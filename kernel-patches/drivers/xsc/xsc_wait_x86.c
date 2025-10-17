// SPDX-License-Identifier: GPL-2.0
/*
 * XSC x86-64 Wait Mechanism Implementation
 *
 * UMONITOR/UMWAIT with PAUSE fallback
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/cpufeature.h>
#include <asm/cpufeature.h>
#include <asm/msr.h>

#include "../../include/xsc_wait.h"

/* CPUID feature bits */
#define X86_FEATURE_WAITPKG_BIT	5	/* UMONITOR/UMWAIT/TPAUSE */

/* MSR for UMWAIT control */
#define MSR_IA32_UMWAIT_CONTROL	0xe1

/*
 * Detect x86-64 wait capabilities
 */
int xsc_wait_detect_x86(struct xsc_wait_mechanism *mech)
{
	u32 eax, ebx, ecx, edx;

	pr_info("xsc_wait: Detecting x86-64 wait mechanisms\n");

	/* Check for WAITPKG (UMONITOR/UMWAIT) support */
	cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
	mech->has_umwait = !!(ecx & (1 << X86_FEATURE_WAITPKG_BIT));

	if (mech->has_umwait) {
		u64 umwait_control;

		pr_info("xsc_wait: UMONITOR/UMWAIT detected\n");

		/* Check if UMWAIT is enabled */
		rdmsrl(MSR_IA32_UMWAIT_CONTROL, umwait_control);

		if (umwait_control & 0x1) {
			pr_warn("xsc_wait: UMWAIT disabled via MSR (bit 0 set)\n");
			pr_warn("xsc_wait: OS/hypervisor has disabled UMWAIT\n");
			mech->has_umwait = false;
		} else {
			pr_info("xsc_wait: UMWAIT enabled and available\n");
		}
	}

	/* PAUSE is always available on x86-64 */
	pr_info("xsc_wait: PAUSE available (universal fallback)\n");

	/* Set primary and fallback */
	if (mech->has_umwait) {
		mech->primary = XSC_WAIT_UMWAIT;
		mech->fallback = XSC_WAIT_PAUSE;
		snprintf(mech->name, sizeof(mech->name), "x86_umwait+pause");
	} else {
		mech->primary = XSC_WAIT_PAUSE;
		mech->fallback = XSC_WAIT_FUTEX;
		snprintf(mech->name, sizeof(mech->name), "x86_pause");
	}

	/* Set thresholds */
	mech->thresholds.max_latency_ns = XSC_WAIT_X86_MAX_LATENCY_NS;
	mech->thresholds.p99_latency_ns = XSC_WAIT_X86_P99_LATENCY_NS;
	mech->thresholds.spin_threshold_ns = XSC_WAIT_X86_SPIN_THRESHOLD_NS;
	mech->thresholds.min_success_rate_pct = XSC_WAIT_X86_MIN_SUCCESS_PCT;
	mech->thresholds.max_spurious_pct = XSC_WAIT_X86_MAX_SPURIOUS_PCT;

	mech->state = XSC_WAIT_CANDIDATE;
	return 0;
}

/*
 * UMONITOR/UMWAIT wrapper
 * Returns cycles waited
 */
static inline u64 do_umwait(volatile u64 *addr, u64 old, u64 timeout_cycles)
{
	u64 t0 = xsc_rdtsc();
	u64 deadline = t0 + timeout_cycles;
	u32 tsc_low, tsc_high;
	u8 result;

	/* Set up monitor on address */
	asm volatile(
		"umonitor %0"
		:
		: "r" (addr)
		: "memory"
	);

	/* Check if value changed (avoid spurious wait) */
	if (*addr != old)
		return xsc_rdtsc() - t0;

	/* Wait with timeout */
	tsc_low = (u32)deadline;
	tsc_high = (u32)(deadline >> 32);

	asm volatile(
		"umwait %[state]\n\t"
		"setc %[result]"
		: [result] "=r" (result)
		: "d" (tsc_high), "a" (tsc_low),
		  [state] "r" ((u32)0)  /* C0.2 state */
		: "cc", "memory"
	);

	return xsc_rdtsc() - t0;
}

/*
 * PAUSE-based adaptive spin
 * Three-phase: tight spin → relaxed spin → futex
 */
static inline u64 do_pause_spin(volatile u64 *addr, u64 old, u64 timeout_cycles)
{
	u64 t0 = xsc_rdtsc();
	u64 deadline = t0 + timeout_cycles;
	int i;

	/* Phase 1: Tight spin (0-1µs) */
	for (i = 0; i < 100; i++) {
		if (*addr != old)
			return xsc_rdtsc() - t0;
		asm volatile("pause" ::: "memory");
	}

	/* Phase 2: Relaxed spin (1-timeout) */
	while (xsc_rdtsc() < deadline) {
		if (*addr != old)
			return xsc_rdtsc() - t0;

		/* 10x PAUSE for reduced power */
		for (i = 0; i < 10; i++)
			asm volatile("pause" ::: "memory");
	}

	/* Timeout */
	return xsc_rdtsc() - t0;
}

/*
 * Validate UMWAIT functionality
 */
static int validate_umwait(struct xsc_wait_mechanism *mech)
{
	volatile u64 test_var = 0;
	u64 latencies[1000];
	u64 timeout_cycles = xsc_ns_to_cycles(100000); /* 100µs */
	int i;

	pr_info("xsc_wait: Validating UMWAIT (1000 iterations)\n");

	for (i = 0; i < 1000; i++) {
		u64 t0 = xsc_rdtsc();

		/* Set up UMONITOR */
		asm volatile("umonitor %0" :: "r" (&test_var) : "memory");

		/* Immediately wake by changing value */
		test_var = 1;

		/* Try to wait (should wake immediately) */
		u32 tsc_low = (u32)(t0 + timeout_cycles);
		u32 tsc_high = (u32)((t0 + timeout_cycles) >> 32);

		asm volatile(
			"umwait %0"
			:
			: "r" ((u32)0), "d" (tsc_high), "a" (tsc_low)
			: "cc", "memory"
		);

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
		u64 max_ns = xsc_cycles_to_ns(max);

		pr_info("xsc_wait: UMWAIT latency: min=%llu cycles, avg=%llu ns, max=%llu ns\n",
			min, avg_ns, max_ns);

		/* Check thresholds */
		if (max_ns > mech->thresholds.max_latency_ns) {
			snprintf(mech->fail_reason, sizeof(mech->fail_reason),
				 "UMWAIT max latency %llu ns exceeds threshold %llu ns",
				 max_ns, mech->thresholds.max_latency_ns);
			return -EINVAL;
		}

		if (avg_ns > mech->thresholds.p99_latency_ns) {
			pr_warn("xsc_wait: UMWAIT avg latency %llu ns exceeds P99 %llu ns\n",
				avg_ns, mech->thresholds.p99_latency_ns);
			pr_warn("xsc_wait: Marking as DEGRADED but continuing\n");
			mech->state = XSC_WAIT_DEGRADED;
		}
	}

	pr_info("xsc_wait: UMWAIT validation PASSED\n");
	return 0;
}

/*
 * Validate PAUSE functionality
 */
static int validate_pause(struct xsc_wait_mechanism *mech)
{
	volatile u64 test_var = 0;
	u64 latencies[1000];
	int i;

	pr_info("xsc_wait: Validating PAUSE spin (1000 iterations)\n");

	for (i = 0; i < 1000; i++) {
		u64 t0 = xsc_rdtsc();

		/* Spin for a few cycles */
		for (int j = 0; j < 10; j++) {
			if (test_var)
				break;
			asm volatile("pause" ::: "memory");
		}

		/* Wake by setting variable */
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

		pr_info("xsc_wait: PAUSE latency: min=%llu cycles, avg=%llu ns, max=%llu cycles\n",
			min, avg_ns, max);
	}

	pr_info("xsc_wait: PAUSE validation PASSED\n");
	return 0;
}

/*
 * Full x86-64 validation
 */
int xsc_wait_validate_x86(struct xsc_wait_mechanism *mech)
{
	int ret;

	pr_info("xsc_wait: Starting x86-64 validation\n");

	mech->state = XSC_WAIT_VALIDATING;

	/* Validate primary mechanism */
	if (mech->primary == XSC_WAIT_UMWAIT) {
		ret = validate_umwait(mech);
		if (ret) {
			pr_warn("xsc_wait: UMWAIT validation failed, falling back to PAUSE\n");
			mech->primary = XSC_WAIT_PAUSE;
			mech->has_umwait = false;
		}
	}

	/* Always validate PAUSE (fallback) */
	ret = validate_pause(mech);
	if (ret) {
		snprintf(mech->fail_reason, sizeof(mech->fail_reason),
			 "PAUSE validation failed (should never happen)");
		return ret;
	}

	/* Promote to active */
	if (mech->state != XSC_WAIT_DEGRADED)
		mech->state = XSC_WAIT_ACTIVE;

	pr_info("xsc_wait: x86-64 validation PASSED (primary: %s)\n",
		mech->primary == XSC_WAIT_UMWAIT ? "UMWAIT" : "PAUSE");

	return 0;
}

/*
 * Main wait operation for x86-64
 *
 * @addr: Address to monitor
 * @old: Expected old value (wake when *addr != old)
 * @timeout_ns: Maximum time to wait (0 = no timeout)
 *
 * Returns: Cycles waited
 */
u64 xsc_wait_x86(struct xsc_wait_mechanism *mech,
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
	if (mech->primary == XSC_WAIT_UMWAIT && mech->state == XSC_WAIT_ACTIVE) {
		elapsed = do_umwait(addr, old, timeout_cycles);
		atomic64_inc(&mech->stats.deep_sleeps);
	} else {
		elapsed = do_pause_spin(addr, old, timeout_cycles);
		atomic64_inc(&mech->stats.shallow_spins);
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

MODULE_LICENSE("GPL");
