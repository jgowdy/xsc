/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XSC Cross-Architecture Wait Mechanism
 *
 * Unified API for optimal waiting on x86-64 and ARM64.
 * Never trust hardware - validate everything.
 */

#ifndef _XSC_WAIT_H
#define _XSC_WAIT_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/kobject.h>

/* Wait mechanism types */
enum xsc_wait_type {
	XSC_WAIT_NONE = 0,	/* Not initialized */
	XSC_WAIT_UMWAIT,	/* x86-64: UMONITOR/UMWAIT */
	XSC_WAIT_PAUSE,		/* x86-64: PAUSE-based spin */
	XSC_WAIT_WFE,		/* ARM64: WFE/SEV */
	XSC_WAIT_DOORBELL,	/* ARM64: Hardware doorbell */
	XSC_WAIT_GIC_LPI,	/* ARM64: GICv3 LPI */
	XSC_WAIT_FUTEX,		/* Generic: futex fallback */
};

/* Wait mechanism state */
enum xsc_wait_state {
	XSC_WAIT_CANDIDATE = 0,	/* Detected but unvalidated */
	XSC_WAIT_VALIDATING,	/* Running validation tests */
	XSC_WAIT_ACTIVE,	/* Validated and in use */
	XSC_WAIT_DEGRADED,	/* Working but sub-optimal */
	XSC_WAIT_FAILED,	/* Validation failed */
};

/* Per-mechanism statistics */
struct xsc_wait_stats {
	atomic64_t total_waits;
	atomic64_t successful_waits;
	atomic64_t timeouts;
	atomic64_t spurious_wakes;

	/* Latency tracking (nanoseconds) */
	atomic64_t min_latency_ns;
	atomic64_t max_latency_ns;
	atomic64_t total_latency_ns;

	/* Power state tracking */
	atomic64_t deep_sleeps;		/* Times entered deep wait */
	atomic64_t shallow_spins;	/* Times spin-waited */

	/* Degradation tracking */
	atomic64_t validation_failures;
	atomic64_t watchdog_triggers;
};

/* Validation thresholds */
struct xsc_wait_thresholds {
	u64 max_latency_ns;		/* Maximum acceptable latency */
	u64 p99_latency_ns;		/* 99th percentile target */
	u64 spin_threshold_ns;		/* When to stop spinning */
	u32 min_success_rate_pct;	/* Minimum success rate */
	u32 max_spurious_pct;		/* Maximum spurious wake rate */
};

/* Default thresholds for x86-64 */
#define XSC_WAIT_X86_MAX_LATENCY_NS	1000000	/* 1ms */
#define XSC_WAIT_X86_P99_LATENCY_NS	200000	/* 200µs */
#define XSC_WAIT_X86_SPIN_THRESHOLD_NS	10000	/* 10µs */
#define XSC_WAIT_X86_MIN_SUCCESS_PCT	95	/* 95% */
#define XSC_WAIT_X86_MAX_SPURIOUS_PCT	5	/* 5% */

/* Wait mechanism descriptor */
struct xsc_wait_mechanism {
	enum xsc_wait_type type;
	enum xsc_wait_state state;

	/* Hardware capabilities */
	bool has_umwait;	/* x86-64: UMONITOR/UMWAIT */
	bool has_wfe;		/* ARM64: WFE/SEV */
	bool has_doorbell;	/* ARM64: Hardware doorbell */
	bool has_gic_lpi;	/* ARM64: GICv3 LPI */
	bool has_gicv4;		/* ARM64: GICv4 direct injection */

	/* Active configuration */
	enum xsc_wait_type primary;
	enum xsc_wait_type fallback;

	/* Statistics & thresholds */
	struct xsc_wait_stats stats;
	struct xsc_wait_thresholds thresholds;

	/* Watchdog */
	struct delayed_work watchdog_work;
	atomic_t watchdog_failures;

	/* GIC-specific (ARM64) */
	struct completion gic_wait_complete;

	/* sysfs */
	struct kobject kobj;

	char name[32];
	char fail_reason[128];
};

/* Architecture-specific detection */
#ifdef CONFIG_X86_64
int xsc_wait_detect_x86(struct xsc_wait_mechanism *mech);
int xsc_wait_validate_x86(struct xsc_wait_mechanism *mech);
u64 xsc_wait_x86(struct xsc_wait_mechanism *mech, volatile u64 *addr, u64 old, u64 timeout_ns);
#endif

#ifdef CONFIG_ARM64
int xsc_wait_detect_arm64(struct xsc_wait_mechanism *mech);
int xsc_wait_validate_arm64(struct xsc_wait_mechanism *mech);
u64 xsc_wait_arm64(struct xsc_wait_mechanism *mech, volatile u64 *addr, u64 old, u64 timeout_ns);
void xsc_wait_cleanup_arm64(void);

/* ARM64 GICv3/GICv4 support */
int xsc_gic_init(struct xsc_wait_mechanism *mech);
int xsc_gic_validate(struct xsc_wait_mechanism *mech);
void xsc_gic_cleanup(void);
#endif

/* Core API */
int xsc_wait_init(void);
void xsc_wait_cleanup(void);
int xsc_wait_validate(struct xsc_wait_mechanism *mech);
void xsc_wait_rollback(struct xsc_wait_mechanism *mech, const char *reason);

/* Wait operation - unified API across architectures */
static inline u64 xsc_wait(struct xsc_wait_mechanism *mech,
			   volatile u64 *addr, u64 old, u64 timeout_ns)
{
#ifdef CONFIG_X86_64
	return xsc_wait_x86(mech, addr, old, timeout_ns);
#elif defined(CONFIG_ARM64)
	return xsc_wait_arm64(mech, addr, old, timeout_ns);
#else
	return 0; /* Unsupported architecture */
#endif
}

/* Watchdog */
void xsc_wait_watchdog_init(struct xsc_wait_mechanism *mech);
void xsc_wait_watchdog_check(struct work_struct *work);
void xsc_wait_watchdog_stop(struct xsc_wait_mechanism *mech);

/* sysfs interface */
int xsc_wait_sysfs_init(struct xsc_wait_mechanism *mech);
void xsc_wait_sysfs_cleanup(struct xsc_wait_mechanism *mech);

/* Utility functions */
static inline u64 xsc_wait_avg_latency(struct xsc_wait_stats *stats)
{
	u64 total = atomic64_read(&stats->total_latency_ns);
	u64 count = atomic64_read(&stats->successful_waits);

	return count ? total / count : 0;
}

static inline u64 xsc_wait_success_rate(struct xsc_wait_stats *stats)
{
	u64 total = atomic64_read(&stats->total_waits);
	u64 success = atomic64_read(&stats->successful_waits);

	return total ? (success * 100) / total : 0;
}

static inline u64 xsc_wait_spurious_rate(struct xsc_wait_stats *stats)
{
	u64 total = atomic64_read(&stats->total_waits);
	u64 spurious = atomic64_read(&stats->spurious_wakes);

	return total ? (spurious * 100) / total : 0;
}

/* CPU frequency for TSC conversion (initialized at boot) */
extern u64 xsc_tsc_freq_ghz;

/* Timestamp functions */
static inline u64 xsc_rdtsc(void)
{
#ifdef CONFIG_X86_64
	u32 lo, hi;
	asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((u64)hi << 32) | lo;
#elif defined(CONFIG_ARM64)
	u64 val;
	asm volatile("mrs %0, cntvct_el0" : "=r"(val));
	return val;
#else
	return 0;
#endif
}

static inline u64 xsc_cycles_to_ns(u64 cycles)
{
	return cycles / xsc_tsc_freq_ghz;
}

static inline u64 xsc_ns_to_cycles(u64 ns)
{
	return ns * xsc_tsc_freq_ghz;
}

/* Constants */
#define XSC_WAIT_WATCHDOG_INTERVAL_SEC	10
#define XSC_WAIT_MAX_WATCHDOG_FAILURES	3

#endif /* _XSC_WAIT_H */
