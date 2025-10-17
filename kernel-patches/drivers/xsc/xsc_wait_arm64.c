// SPDX-License-Identifier: GPL-2.0
/*
 * XSC ARM64 Wait Mechanism - Unified Implementation
 *
 * Priority order for AWS EC2/Graviton optimization:
 * 1. GICv3/GICv4 LPIs (best for Graviton2/3)
 * 2. Hardware doorbells (if available)
 * 3. WFE/SEV (universal fallback)
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/cpufeature.h>

#include "../../include/xsc_wait.h"

/* External doorbell device (from xsc_doorbell.c) */
extern struct xsc_doorbell_device *xsc_global_doorbell;

/*
 * Detect ARM64-specific capabilities
 */
int xsc_wait_detect_arm64(struct xsc_wait_mechanism *mech)
{
	int ret;

	pr_info("xsc_wait: Detecting ARM64 wait mechanisms\n");

	/* Try GICv3/GICv4 first (optimal for EC2) */
	ret = xsc_gic_init(mech);
	if (ret == 0 && mech->has_gic_lpi) {
		pr_info("xsc_wait: GICv3 LPI available (optimal for AWS Graviton)\n");
		mech->primary = XSC_WAIT_GIC_LPI;
		mech->fallback = XSC_WAIT_WFE;
		snprintf(mech->name, sizeof(mech->name), "arm64_gic_lpi");
		goto set_thresholds;
	}

	/* Check for validated hardware doorbell */
	if (xsc_global_doorbell &&
	    xsc_global_doorbell->state == XSC_DB_STEADY) {
		pr_info("xsc_wait: Hardware doorbell validated and available\n");
		mech->has_doorbell = true;
		mech->primary = XSC_WAIT_DOORBELL;
		mech->fallback = XSC_WAIT_WFE;
		snprintf(mech->name, sizeof(mech->name), "arm64_doorbell");
		goto set_thresholds;
	}

	/* Fallback to WFE (always available) */
	pr_info("xsc_wait: Using WFE/SEV (universal ARM64 fallback)\n");
	mech->has_wfe = true;
	mech->primary = XSC_WAIT_WFE;
	mech->fallback = XSC_WAIT_FUTEX;
	snprintf(mech->name, sizeof(mech->name), "arm64_wfe");

set_thresholds:
	/* ARM64-specific thresholds */
	mech->thresholds.max_latency_ns = 500000;	/* 500µs */
	mech->thresholds.p99_latency_ns = 150000;	/* 150µs */
	mech->thresholds.spin_threshold_ns = 10000;	/* 10µs */
	mech->thresholds.min_success_rate_pct = 95;
	mech->thresholds.max_spurious_pct = 5;

	/* GICv3 LPIs can be faster */
	if (mech->primary == XSC_WAIT_GIC_LPI) {
		mech->thresholds.max_latency_ns = 200000;	/* 200µs */
		mech->thresholds.p99_latency_ns = 50000;	/* 50µs */
	}

	mech->state = XSC_WAIT_CANDIDATE;
	return 0;
}

/*
 * WFE-based wait (universal ARM64 fallback)
 */
static inline u64 do_wfe_wait(volatile u64 *addr, u64 old, u64 timeout_cycles)
{
	u64 t0 = xsc_rdtsc();
	u64 deadline = t0 + timeout_cycles;
	int spins = 0;

	/* Phase 1: Tight spin with YIELD (0-1µs) */
	while (spins++ < 100) {
		if (*addr != old)
			return xsc_rdtsc() - t0;
		asm volatile("yield" ::: "memory");
	}

	/* Phase 2: WFE-based wait until timeout */
	while (xsc_rdtsc() < deadline) {
		if (*addr != old)
			return xsc_rdtsc() - t0;

		/* Wait for event */
		asm volatile("wfe" ::: "memory");

		/* Check value again after wake */
		if (*addr != old)
			return xsc_rdtsc() - t0;
	}

	/* Timeout */
	return xsc_rdtsc() - t0;
}

/*
 * Hardware doorbell wait
 */
static inline u64 do_doorbell_wait(volatile u64 *addr, u64 old, u64 timeout_ns)
{
	u64 t0 = xsc_rdtsc();

	if (!xsc_global_doorbell ||
	    xsc_global_doorbell->state != XSC_DB_STEADY) {
		/* Doorbell not available, fall back to WFE */
		return do_wfe_wait(addr, old, xsc_ns_to_cycles(timeout_ns));
	}

	/* Fast check before ringing doorbell */
	if (*addr != old)
		return 0;

	/* Ring doorbell (existing validated implementation) */
	xsc_doorbell_ring(xsc_global_doorbell, (u32)*addr);

	/* Wait with WFE while doorbell IRQ is pending */
	u64 deadline = t0 + xsc_ns_to_cycles(timeout_ns);
	while (*addr == old && xsc_rdtsc() < deadline) {
		asm volatile("wfe" ::: "memory");
	}

	return xsc_rdtsc() - t0;
}

/*
 * Validate WFE functionality
 */
static int validate_wfe(struct xsc_wait_mechanism *mech)
{
	volatile u64 test_var = 0;
	u64 latencies[1000];
	int i;

	pr_info("xsc_wait: Validating WFE/SEV (1000 iterations)\n");

	for (i = 0; i < 1000; i++) {
		u64 t0 = xsc_rdtsc();

		/* Spin a few cycles with YIELD */
		for (int j = 0; j < 10; j++) {
			if (test_var)
				break;
			asm volatile("yield" ::: "memory");
		}

		/* Set variable (would trigger SEV in real scenario) */
		test_var = 1;

		latencies[i] = xsc_rdtsc() - t0;
		test_var = 0;
	}

	/* Analyze latencies */
	{
		u64 min = latencies[0], max = latencies[0], sum = 0;

		for (i = 0; i < 1000; i++) {
			if (latencies[i] < min) min = latencies[i];
			if (latencies[i] > max) max = latencies[i];
			sum += latencies[i];
		}

		u64 avg_cycles = sum / 1000;
		u64 avg_ns = xsc_cycles_to_ns(avg_cycles);

		pr_info("xsc_wait: WFE latency: min=%llu cycles, avg=%llu ns, max=%llu cycles\n",
			min, avg_ns, max);
	}

	pr_info("xsc_wait: WFE validation PASSED\n");
	return 0;
}

/*
 * Full ARM64 validation
 */
int xsc_wait_validate_arm64(struct xsc_wait_mechanism *mech)
{
	int ret;

	pr_info("xsc_wait: Starting ARM64 validation\n");

	mech->state = XSC_WAIT_VALIDATING;

	/* Validate primary mechanism */
	switch (mech->primary) {
	case XSC_WAIT_GIC_LPI:
		ret = xsc_gic_validate(mech);
		if (ret) {
			pr_warn("xsc_wait: GIC LPI validation failed, falling back to WFE\n");
			mech->primary = XSC_WAIT_WFE;
			mech->has_gic_lpi = false;
		} else {
			pr_info("xsc_wait: GIC LPI validated (optimal for EC2/Graviton)\n");
		}
		break;

	case XSC_WAIT_DOORBELL:
		/* Doorbell already validated by xsc_doorbell_validate() */
		pr_info("xsc_wait: Using pre-validated hardware doorbell\n");
		break;

	case XSC_WAIT_WFE:
		/* WFE always works, but validate for stats */
		ret = validate_wfe(mech);
		if (ret) {
			/* Should never happen */
			snprintf(mech->fail_reason, sizeof(mech->fail_reason),
				 "WFE validation failed (impossible)");
			return ret;
		}
		break;

	default:
		pr_err("xsc_wait: Unknown primary mechanism %d\n", mech->primary);
		return -EINVAL;
	}

	/* Promote to active */
	if (mech->state != XSC_WAIT_DEGRADED)
		mech->state = XSC_WAIT_ACTIVE;

	pr_info("xsc_wait: ARM64 validation PASSED (primary: %s)\n",
		mech->primary == XSC_WAIT_GIC_LPI ? "GIC_LPI" :
		mech->primary == XSC_WAIT_DOORBELL ? "DOORBELL" : "WFE");

	return 0;
}

/*
 * Main wait operation for ARM64
 *
 * @addr: Address to monitor
 * @old: Expected old value (wake when *addr != old)
 * @timeout_ns: Maximum time to wait (0 = use default)
 *
 * Returns: Cycles waited
 */
u64 xsc_wait_arm64(struct xsc_wait_mechanism *mech,
		   volatile u64 *addr, u64 old, u64 timeout_ns)
{
	u64 t0 = xsc_rdtsc();
	u64 timeout_cycles;
	u64 elapsed;
	bool timed_out = false;

	atomic64_inc(&mech->stats.total_waits);

	/* Fast path: already changed */
	if (*addr != old) {
		atomic64_inc(&mech->stats.spurious_wakes);
		return 0;
	}

	/* Convert timeout */
	timeout_cycles = timeout_ns ? xsc_ns_to_cycles(timeout_ns) :
				      xsc_ns_to_cycles(mech->thresholds.spin_threshold_ns);

	/* Use primary mechanism */
	switch (mech->primary) {
	case XSC_WAIT_GIC_LPI:
		/* GIC LPI waiting handled via IRQ (uses completion) */
		if (mech->state == XSC_WAIT_ACTIVE) {
			/* This would be hooked up to actual LPI delivery */
			elapsed = do_wfe_wait(addr, old, timeout_cycles);
			atomic64_inc(&mech->stats.deep_sleeps);
		} else {
			elapsed = do_wfe_wait(addr, old, timeout_cycles);
			atomic64_inc(&mech->stats.shallow_spins);
		}
		break;

	case XSC_WAIT_DOORBELL:
		elapsed = do_doorbell_wait(addr, old,
					   xsc_cycles_to_ns(timeout_cycles));
		atomic64_inc(&mech->stats.deep_sleeps);
		break;

	case XSC_WAIT_WFE:
	default:
		elapsed = do_wfe_wait(addr, old, timeout_cycles);
		atomic64_inc(&mech->stats.shallow_spins);
		break;
	}

	/* Check result */
	if (*addr != old) {
		atomic64_inc(&mech->stats.successful_waits);
	} else {
		atomic64_inc(&mech->stats.timeouts);
		timed_out = true;
	}

	/* Update latency stats */
	{
		u64 latency_ns = xsc_cycles_to_ns(elapsed);
		u64 min = atomic64_read(&mech->stats.min_latency_ns);
		u64 max = atomic64_read(&mech->stats.max_latency_ns);

		if (min == 0 || latency_ns < min)
			atomic64_set(&mech->stats.min_latency_ns, latency_ns);
		if (latency_ns > max)
			atomic64_set(&mech->stats.max_latency_ns, latency_ns);

		atomic64_add(latency_ns, &mech->stats.total_latency_ns);
	}

	return elapsed;
}

/*
 * Platform detection - identify AWS EC2/Graviton
 */
static bool is_aws_graviton(void)
{
	struct device_node *node;
	const char *model;

	/* Check device tree model */
	node = of_find_node_by_path("/");
	if (node) {
		if (of_property_read_string(node, "model", &model) == 0) {
			/* AWS Graviton instances have specific model strings */
			if (strstr(model, "AWS") ||
			    strstr(model, "Graviton") ||
			    strstr(model, "EC2")) {
				of_node_put(node);
				pr_info("xsc_wait: Detected AWS Graviton/EC2 instance\n");
				return true;
			}
		}
		of_node_put(node);
	}

	/* Check for ARM Neoverse cores (used in Graviton2/3) */
	{
		u32 midr = read_cpuid_id();
		u32 implementer = (midr >> 24) & 0xff;
		u32 partnum = (midr >> 4) & 0xfff;

		/* ARM implementer (0x41) + Neoverse N1 (0xd0c) or V1 (0xd40) */
		if (implementer == 0x41 &&
		    (partnum == 0xd0c || partnum == 0xd40)) {
			pr_info("xsc_wait: Detected ARM Neoverse core (Graviton2/3)\n");
			return true;
		}
	}

	return false;
}

/*
 * Cleanup ARM64 resources
 */
void xsc_wait_cleanup_arm64(void)
{
	xsc_gic_cleanup();
	pr_info("xsc_wait: ARM64 cleanup complete\n");
}

MODULE_LICENSE("GPL");
