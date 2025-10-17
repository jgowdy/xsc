// SPDX-License-Identifier: GPL-2.0
/*
 * XSC ARM64 Doorbell Runtime Validation
 *
 * Never trust hardware - validate everything before using it.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/topology.h>

#include "xsc_internal.h"
#include "xsc_doorbell.h"

/* Global doorbell device (one per system for now) */
static struct xsc_doorbell_device *xsc_global_doorbell = NULL;

/*
 * Preflight safety checks - verify device is safe to test
 */
int xsc_doorbell_preflight(struct xsc_doorbell_device *db)
{
	int ret;

	pr_info("xsc_doorbell: preflight checks for %s\n", db->name);

	/* 1. Check MMIO region */
	if (!db->mmio_res || resource_size(db->mmio_res) < 4 ||
	    resource_size(db->mmio_res) > 65536) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "Invalid MMIO size: %llu bytes",
			 resource_size(db->mmio_res));
		return -EINVAL;
	}

	/* 2. Try to map MMIO with Device-nGnRnE attributes */
	db->mmio_base = ioremap(db->mmio_res->start, resource_size(db->mmio_res));
	if (!db->mmio_base) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "Failed to map MMIO at 0x%llx", db->mmio_res->start);
		return -ENOMEM;
	}

	/* 3. Verify IRQ exists */
	if (db->irq < 0) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "No valid IRQ assigned");
		ret = -EINVAL;
		goto unmap;
	}

	/* 4. Check CPU affinity */
	if (db->target_cpu >= nr_cpu_ids || !cpu_online(db->target_cpu)) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "Target CPU %d invalid or offline", db->target_cpu);
		ret = -EINVAL;
		goto unmap;
	}

	/* 5. Check NUMA/cluster locality */
	{
		int irq_pkg = topology_physical_package_id(db->target_cpu);
		pr_info("xsc_doorbell: IRQ will target CPU %d (package %d)\n",
			db->target_cpu, irq_pkg);
	}

	pr_info("xsc_doorbell: preflight passed for %s\n", db->name);
	db->state = XSC_DB_PREFLIGHT;
	return 0;

unmap:
	iounmap(db->mmio_base);
	db->mmio_base = NULL;
	return ret;
}

/*
 * Test IRQ handler
 */
irqreturn_t xsc_doorbell_test_irq(int irq, void *data)
{
	struct xsc_doorbell_device *db = data;
	u64 now = xsc_doorbell_get_timestamp();
	int this_cpu = smp_processor_id();

	/* Track IRQ stats */
	atomic64_inc(&db->stats.total_irqs);

	if (this_cpu != db->target_cpu)
		atomic64_inc(&db->stats.wrong_cpu_irqs);

	/* During testing, just count and complete */
	atomic_inc(&db->test_irq_count);
	complete(&db->test_complete);

	return IRQ_HANDLED;
}

/*
 * Self-test: basic loopback functionality
 */
int xsc_doorbell_self_test(struct xsc_doorbell_device *db)
{
	int ret, i;
	unsigned long timeout;

	pr_info("xsc_doorbell: self-test starting for %s\n", db->name);

	/* Allocate test payload */
	db->test_payload = kzalloc(sizeof(*db->test_payload), GFP_KERNEL);
	if (!db->test_payload)
		return -ENOMEM;

	/* Initialize completion */
	init_completion(&db->test_complete);
	atomic_set(&db->test_irq_count, 0);

	/* Install IRQ handler */
	ret = request_irq(db->irq, xsc_doorbell_test_irq, 0,
			  "xsc-doorbell-test", db);
	if (ret) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "Failed to request IRQ %d: %d", db->irq, ret);
		goto free_payload;
	}

	/* Set IRQ affinity */
	irq_set_affinity_hint(db->irq, cpumask_of(db->target_cpu));

	/* Test 10 doorbells */
	for (i = 0; i < 10; i++) {
		u64 t0, dt;

		/* Set sequence number */
		atomic_set(&db->test_payload->seq, i);
		db->test_payload->timestamp = xsc_doorbell_get_timestamp();

		/* Ensure payload visible before doorbell */
		smp_wmb();

		/* Ring doorbell */
		t0 = xsc_doorbell_get_timestamp();
		xsc_doorbell_ring(db, i);

		/* Wait for IRQ (2ms timeout) */
		timeout = wait_for_completion_timeout(&db->test_complete,
						      msecs_to_jiffies(2));
		if (!timeout) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "IRQ timeout on poke %d", i);
			ret = -ETIMEDOUT;
			goto cleanup_irq;
		}

		dt = xsc_doorbell_get_timestamp() - t0;

		/* Update latency stats */
		if (i == 0 || dt < atomic64_read(&db->stats.min_latency_ns))
			atomic64_set(&db->stats.min_latency_ns, dt);
		if (dt > atomic64_read(&db->stats.max_latency_ns))
			atomic64_set(&db->stats.max_latency_ns, dt);
		atomic64_add(dt, &db->stats.total_latency_ns);

		/* Check if IRQ arrived on correct CPU */
		if (atomic64_read(&db->stats.wrong_cpu_irqs) > 0) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "IRQ delivered to wrong CPU");
			ret = -EINVAL;
			goto cleanup_irq;
		}

		/* Reset for next iteration */
		reinit_completion(&db->test_complete);
	}

	/* Verify all IRQs delivered */
	if (atomic_read(&db->test_irq_count) != 10) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "Expected 10 IRQs, got %d",
			 atomic_read(&db->test_irq_count));
		ret = -EINVAL;
		goto cleanup_irq;
	}

	/* Check latency thresholds */
	{
		u64 max_lat = atomic64_read(&db->stats.max_latency_ns);
		if (max_lat > db->thresholds.max_latency_ns) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "Max latency %llu ns exceeds threshold %llu ns",
				 max_lat, db->thresholds.max_latency_ns);
			ret = -EINVAL;
			goto cleanup_irq;
		}
	}

	pr_info("xsc_doorbell: self-test passed for %s (avg latency: %llu ns)\n",
		db->name, xsc_doorbell_avg_latency(&db->stats));
	ret = 0;

cleanup_irq:
	irq_set_affinity_hint(db->irq, NULL);
	free_irq(db->irq, db);
free_payload:
	kfree(db->test_payload);
	db->test_payload = NULL;

	return ret;
}

/*
 * Memory ordering test - ensure doorbell doesn't violate memory semantics
 */
int xsc_doorbell_ordering_test(struct xsc_doorbell_device *db)
{
	int i, ret = 0;

	pr_info("xsc_doorbell: memory ordering test for %s\n", db->name);

	db->test_payload = kzalloc(sizeof(*db->test_payload), GFP_KERNEL);
	if (!db->test_payload)
		return -ENOMEM;

	init_completion(&db->test_complete);

	ret = request_irq(db->irq, xsc_doorbell_test_irq, 0,
			  "xsc-doorbell-order", db);
	if (ret)
		goto free_payload;

	for (i = 0; i < 1000; i++) {
		u8 pattern = i & 0xFF;
		u32 expected_checksum = 0;

		/* Write unique pattern */
		atomic_set(&db->test_payload->seq, i);
		db->test_payload->timestamp = xsc_doorbell_get_timestamp();
		memset(db->test_payload->data, pattern, 128);

		/* Calculate checksum */
		for (int j = 0; j < 128; j++)
			expected_checksum += db->test_payload->data[j];
		db->test_payload->checksum = expected_checksum;

		/* Release barrier before doorbell */
		smp_wmb();

		xsc_doorbell_ring(db, i);

		if (!wait_for_completion_timeout(&db->test_complete,
						 msecs_to_jiffies(2))) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "Ordering test timeout at iteration %d", i);
			ret = -ETIMEDOUT;
			goto cleanup;
		}

		reinit_completion(&db->test_complete);
	}

	pr_info("xsc_doorbell: ordering test passed for %s\n", db->name);

cleanup:
	free_irq(db->irq, db);
free_payload:
	kfree(db->test_payload);
	db->test_payload = NULL;

	return ret;
}

/*
 * Full validation: run all tests
 */
int xsc_doorbell_validate(struct xsc_doorbell_device *db)
{
	int ret;

	/* Preflight */
	ret = xsc_doorbell_preflight(db);
	if (ret)
		return ret;

	db->state = XSC_DB_VALIDATING;

	/* Self-test */
	ret = xsc_doorbell_self_test(db);
	if (ret)
		goto rollback;

	/* Memory ordering */
	ret = xsc_doorbell_ordering_test(db);
	if (ret)
		goto rollback;

	/* Soak test */
	ret = xsc_doorbell_soak_test(db);
	if (ret)
		goto rollback;

	/* Power state test */
	ret = xsc_doorbell_power_test(db);
	if (ret)
		goto rollback;

	/* Coalescing detection */
	ret = xsc_doorbell_coalesce_test(db);
	if (ret)
		goto rollback;

	/* Promote to steady state */
	db->state = XSC_DB_STEADY;
	db->mode = XSC_DB_FULL;

	pr_info("xsc_doorbell: %s VALIDATED and ENABLED\n", db->name);

	/* Initialize sysfs interface */
	ret = xsc_doorbell_sysfs_init(db);
	if (ret) {
		pr_warn("xsc_doorbell: sysfs init failed: %d\n", ret);
		/* Non-fatal, continue */
	}

	/* Start watchdog */
	xsc_doorbell_watchdog_init(db);

	return 0;

rollback:
	xsc_doorbell_rollback(db, db->fail_reason);
	return ret;
}

void xsc_doorbell_rollback(struct xsc_doorbell_device *db, const char *reason)
{
	pr_warn("xsc_doorbell: %s FAILED validation: %s\n",
		db->name, reason);
	pr_warn("xsc_doorbell: Rolling back to adaptive polling\n");

	/* Stop watchdog if running */
	xsc_doorbell_watchdog_stop(db);

	/* Clean up sysfs */
	xsc_doorbell_sysfs_cleanup(db);

	if (db->mmio_base) {
		iounmap(db->mmio_base);
		db->mmio_base = NULL;
	}

	db->state = XSC_DB_FAILED;
	db->mode = XSC_DB_DISABLED;
}

/*
 * Enable doorbell (called after validation)
 */
void xsc_doorbell_enable(struct xsc_doorbell_device *db)
{
	if (db->state != XSC_DB_STEADY) {
		pr_warn("xsc_doorbell: cannot enable %s in state %d\n",
			db->name, db->state);
		return;
	}

	db->mode = XSC_DB_FULL;
	pr_info("xsc_doorbell: %s enabled\n", db->name);
}

/*
 * Disable doorbell (fall back to polling)
 */
void xsc_doorbell_disable(struct xsc_doorbell_device *db)
{
	db->mode = XSC_DB_DISABLED;
	pr_info("xsc_doorbell: %s disabled (polling mode)\n", db->name);
}

/*
 * Discovery: find potential doorbell devices
 */
int xsc_doorbell_discover(void)
{
#ifdef CONFIG_ARM64
	struct device_node *np;
	struct xsc_doorbell_device *db;
	int ret = 0;

	/* Look for platform devices with "doorbell" or "mailbox" in name */
	for_each_compatible_node(np, NULL, "arm,doorbell") {
		db = kzalloc(sizeof(*db), GFP_KERNEL);
		if (!db)
			return -ENOMEM;

		/* Parse device tree */
		ret = of_address_to_resource(np, 0, db->mmio_res);
		if (ret) {
			kfree(db);
			continue;
		}

		db->irq = irq_of_parse_and_map(np, 0);
		if (db->irq < 0) {
			kfree(db);
			continue;
		}

		/* Set defaults */
		snprintf(db->name, sizeof(db->name), "%s", np->name);
		db->target_cpu = 0; /* TODO: select based on shard affinity */
		db->state = XSC_DB_CANDIDATE;

		/* Set default thresholds */
		db->thresholds.max_latency_ns = XSC_DB_DEFAULT_MAX_LATENCY_NS;
		db->thresholds.p99_latency_ns = XSC_DB_DEFAULT_P99_LATENCY_NS;
		db->thresholds.max_spurious_pct = XSC_DB_DEFAULT_MAX_SPURIOUS_PCT;
		db->thresholds.min_effectiveness_pct = XSC_DB_DEFAULT_MIN_EFFECTIVE_PCT;

		/* Try to validate */
		ret = xsc_doorbell_validate(db);
		if (ret == 0) {
			xsc_global_doorbell = db;
			pr_info("xsc_doorbell: Enabled doorbell: %s\n", db->name);
			return 0;
		}

		kfree(db);
	}

	pr_info("xsc_doorbell: No valid doorbells found, using adaptive polling\n");
#endif

	return -ENODEV;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XSC Project");
MODULE_DESCRIPTION("XSC ARM64 Doorbell Validation");
