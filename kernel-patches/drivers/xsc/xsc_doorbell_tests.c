// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Doorbell Extended Test Suite
 *
 * Soak tests, power state tests, coalescing detection
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/cpuidle.h>

#include "xsc_doorbell.h"

/*
 * Soak test: 100,000 doorbells with randomized intervals
 * Validates sustained reliability under production-like workload
 */
int xsc_doorbell_soak_test(struct xsc_doorbell_device *db)
{
	int i, ret = 0;
	unsigned long timeout;
	u32 interval_us;
	u64 latencies[1000]; /* Sample latencies for P99 calculation */
	int latency_idx = 0;

	pr_info("xsc_doorbell: soak test starting for %s (%d pokes)\n",
		db->name, XSC_DB_SOAK_TEST_POKES);

	db->test_payload = kzalloc(sizeof(*db->test_payload), GFP_KERNEL);
	if (!db->test_payload)
		return -ENOMEM;

	init_completion(&db->test_complete);
	atomic_set(&db->test_irq_count, 0);

	ret = request_irq(db->irq, xsc_doorbell_test_irq, 0,
			  "xsc-doorbell-soak", db);
	if (ret)
		goto free_payload;

	irq_set_affinity_hint(db->irq, cpumask_of(db->target_cpu));

	for (i = 0; i < XSC_DB_SOAK_TEST_POKES; i++) {
		u64 t0, dt;

		/* Random interval between pokes */
		interval_us = XSC_DB_SOAK_MIN_INTERVAL_US +
			      (get_random_u32() % (XSC_DB_SOAK_MAX_INTERVAL_US -
						   XSC_DB_SOAK_MIN_INTERVAL_US));

		if (interval_us > 0)
			usleep_range(interval_us, interval_us + 10);

		atomic_set(&db->test_payload->seq, i);
		db->test_payload->timestamp = xsc_doorbell_get_timestamp();
		smp_wmb();

		t0 = xsc_doorbell_get_timestamp();
		xsc_doorbell_ring(db, i);

		timeout = wait_for_completion_timeout(&db->test_complete,
						      msecs_to_jiffies(5));
		if (!timeout) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "Soak test timeout at poke %d/%d",
				 i, XSC_DB_SOAK_TEST_POKES);
			ret = -ETIMEDOUT;
			goto cleanup;
		}

		dt = xsc_doorbell_get_timestamp() - t0;

		/* Update stats */
		atomic64_inc(&db->stats.useful_irqs);
		if (i == 0 || dt < atomic64_read(&db->stats.min_latency_ns))
			atomic64_set(&db->stats.min_latency_ns, dt);
		if (dt > atomic64_read(&db->stats.max_latency_ns))
			atomic64_set(&db->stats.max_latency_ns, dt);
		atomic64_add(dt, &db->stats.total_latency_ns);

		/* Sample every 100th latency for P99 calculation */
		if (i % 100 == 0 && latency_idx < 1000)
			latencies[latency_idx++] = dt;

		/* Check for wrong CPU delivery */
		if (atomic64_read(&db->stats.wrong_cpu_irqs) > 0) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "IRQ delivered to wrong CPU during soak");
			ret = -EINVAL;
			goto cleanup;
		}

		reinit_completion(&db->test_complete);

		/* Progress report every 10,000 pokes */
		if (i > 0 && i % 10000 == 0) {
			pr_info("xsc_doorbell: soak progress %d/%d (avg lat: %llu ns)\n",
				i, XSC_DB_SOAK_TEST_POKES,
				xsc_doorbell_avg_latency(&db->stats));
		}
	}

	/* Verify all IRQs delivered */
	if (atomic_read(&db->test_irq_count) != XSC_DB_SOAK_TEST_POKES) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "Soak test: expected %d IRQs, got %d",
			 XSC_DB_SOAK_TEST_POKES,
			 atomic_read(&db->test_irq_count));
		ret = -EINVAL;
		goto cleanup;
	}

	/* Check final latency thresholds */
	{
		u64 max_lat = atomic64_read(&db->stats.max_latency_ns);
		u64 avg_lat = xsc_doorbell_avg_latency(&db->stats);

		if (max_lat > db->thresholds.max_latency_ns) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "Soak: max latency %llu ns exceeds threshold %llu ns",
				 max_lat, db->thresholds.max_latency_ns);
			ret = -EINVAL;
			goto cleanup;
		}

		if (avg_lat > db->thresholds.p99_latency_ns) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "Soak: avg latency %llu ns exceeds P99 threshold %llu ns",
				 avg_lat, db->thresholds.p99_latency_ns);
			ret = -EINVAL;
			goto cleanup;
		}
	}

	pr_info("xsc_doorbell: soak test PASSED for %s\n", db->name);
	pr_info("  Pokes: %d, Avg latency: %llu ns, Max: %llu ns, Min: %llu ns\n",
		XSC_DB_SOAK_TEST_POKES,
		xsc_doorbell_avg_latency(&db->stats),
		atomic64_read(&db->stats.max_latency_ns),
		atomic64_read(&db->stats.min_latency_ns));

cleanup:
	irq_set_affinity_hint(db->irq, NULL);
	free_irq(db->irq, db);
free_payload:
	kfree(db->test_payload);
	db->test_payload = NULL;

	return ret;
}

/*
 * Power state test: validate wake-from-idle latency
 * Ensures doorbell can wake CPU from deep C-states within acceptable latency
 */
int xsc_doorbell_power_test(struct xsc_doorbell_device *db)
{
	int i, ret = 0;
	unsigned long timeout;
	u64 max_idle_wake = 0;

	pr_info("xsc_doorbell: power state test for %s\n", db->name);

	db->test_payload = kzalloc(sizeof(*db->test_payload), GFP_KERNEL);
	if (!db->test_payload)
		return -ENOMEM;

	init_completion(&db->test_complete);

	ret = request_irq(db->irq, xsc_doorbell_test_irq, 0,
			  "xsc-doorbell-power", db);
	if (ret)
		goto free_payload;

	irq_set_affinity_hint(db->irq, cpumask_of(db->target_cpu));

	for (i = 0; i < XSC_DB_POWER_TEST_CYCLES; i++) {
		u64 t0, wake_lat;

		/* Allow CPU to enter idle state */
		msleep(XSC_DB_POWER_IDLE_MS);

		atomic_set(&db->test_payload->seq, i);
		smp_wmb();

		/* Measure wake latency */
		t0 = xsc_doorbell_get_timestamp();
		xsc_doorbell_ring(db, i);

		timeout = wait_for_completion_timeout(&db->test_complete,
						      msecs_to_jiffies(10));
		if (!timeout) {
			snprintf(db->fail_reason, sizeof(db->fail_reason),
				 "Power test timeout at cycle %d", i);
			ret = -ETIMEDOUT;
			goto cleanup;
		}

		wake_lat = xsc_doorbell_get_timestamp() - t0;

		if (wake_lat > max_idle_wake)
			max_idle_wake = wake_lat;

		/* Track idle wake stats */
		atomic64_inc(&db->stats.idle_to_active_wakes);
		if (wake_lat > atomic64_read(&db->stats.max_idle_wake_ns))
			atomic64_set(&db->stats.max_idle_wake_ns, wake_lat);

		reinit_completion(&db->test_complete);
	}

	/* Check if wake latency is acceptable */
	if (max_idle_wake > db->thresholds.max_latency_ns * 2) {
		snprintf(db->fail_reason, sizeof(db->fail_reason),
			 "Wake-from-idle latency %llu ns too high (threshold: %llu ns)",
			 max_idle_wake, db->thresholds.max_latency_ns * 2);
		ret = -EINVAL;
		goto cleanup;
	}

	pr_info("xsc_doorbell: power test PASSED (max wake: %llu ns)\n",
		max_idle_wake);

cleanup:
	irq_set_affinity_hint(db->irq, NULL);
	free_irq(db->irq, db);
free_payload:
	kfree(db->test_payload);
	db->test_payload = NULL;

	return ret;
}

/*
 * Coalescing detection test: rapid burst of doorbells
 * Detects hardware that coalesces IRQs without software control
 */
int xsc_doorbell_coalesce_test(struct xsc_doorbell_device *db)
{
	int i, ret = 0;
	unsigned long timeout;
	int irq_count_before, irq_count_after;

	pr_info("xsc_doorbell: coalescing detection test for %s\n", db->name);

	db->test_payload = kzalloc(sizeof(*db->test_payload), GFP_KERNEL);
	if (!db->test_payload)
		return -ENOMEM;

	init_completion(&db->test_complete);
	atomic_set(&db->test_irq_count, 0);

	ret = request_irq(db->irq, xsc_doorbell_test_irq, 0,
			  "xsc-doorbell-coal", db);
	if (ret)
		goto free_payload;

	irq_set_affinity_hint(db->irq, cpumask_of(db->target_cpu));

	irq_count_before = atomic_read(&db->test_irq_count);

	/* Fire rapid burst of doorbells */
	for (i = 0; i < XSC_DB_COALESCE_BURST_SIZE; i++) {
		atomic_set(&db->test_payload->seq, i);
		smp_wmb();
		xsc_doorbell_ring(db, i);
		/* No delay - back-to-back doorbells */
	}

	/* Wait for all IRQs (give extra time for coalescing) */
	timeout = msecs_to_jiffies(XSC_DB_COALESCE_BURST_SIZE * 2);
	for (i = 0; i < XSC_DB_COALESCE_BURST_SIZE; i++) {
		if (!wait_for_completion_timeout(&db->test_complete, timeout)) {
			/* Timeout is expected if hardware coalesces */
			break;
		}
		reinit_completion(&db->test_complete);
	}

	irq_count_after = atomic_read(&db->test_irq_count);

	/* Analyze coalescing behavior */
	{
		int irqs_delivered = irq_count_after - irq_count_before;
		int coalesce_ratio = (XSC_DB_COALESCE_BURST_SIZE * 100) /
				     (irqs_delivered > 0 ? irqs_delivered : 1);

		pr_info("xsc_doorbell: coalesce test: %d doorbells -> %d IRQs (ratio: %d%%)\n",
			XSC_DB_COALESCE_BURST_SIZE, irqs_delivered, coalesce_ratio);

		if (coalesce_ratio > 150) {
			/* Hardware is coalescing aggressively (>50% reduction) */
			pr_warn("xsc_doorbell: Hardware coalescing detected (%d%%)\n",
				coalesce_ratio - 100);
			atomic64_add(XSC_DB_COALESCE_BURST_SIZE - irqs_delivered,
				     &db->stats.coalesced_irqs);

			/* Not a failure, but record it */
			db->mode = XSC_DB_COALESCED;
		}
	}

	pr_info("xsc_doorbell: coalescing test completed\n");

cleanup:
	irq_set_affinity_hint(db->irq, NULL);
	free_irq(db->irq, db);
free_payload:
	kfree(db->test_payload);
	db->test_payload = NULL;

	return ret;
}

MODULE_LICENSE("GPL");
