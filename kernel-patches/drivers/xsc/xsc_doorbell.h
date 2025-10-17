/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XSC ARM64 Doorbell Verification & Runtime Validation
 *
 * Progressive trust model for untrusted hardware doorbells:
 * candidate -> preflight -> validate -> steady_state (or rollback)
 */

#ifndef _XSC_DOORBELL_H
#define _XSC_DOORBELL_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/interrupt.h>
#include <linux/completion.h>

/* Doorbell operating modes */
enum xsc_doorbell_mode {
	XSC_DB_DISABLED = 0,	/* Pure adaptive polling */
	XSC_DB_COALESCED,	/* Doorbell every N requests (reduces IRQ rate) */
	XSC_DB_FULL,		/* Doorbell every request */
};

/* Doorbell state machine */
enum xsc_doorbell_state {
	XSC_DB_CANDIDATE = 0,	/* Discovered but unvalidated */
	XSC_DB_PREFLIGHT,	/* Basic safety checks passed */
	XSC_DB_VALIDATING,	/* Running soak tests */
	XSC_DB_STEADY,		/* Promoted to production */
	XSC_DB_FAILED,		/* Validation failed - fell back to polling */
};

/* Validation thresholds (tunable via sysfs) */
struct xsc_doorbell_thresholds {
	u64 max_latency_ns;	/* Maximum acceptable wake latency */
	u64 p99_latency_ns;	/* 99th percentile target */
	u32 max_spurious_pct;	/* Max spurious IRQ percentage (1-100) */
	u32 min_effectiveness_pct; /* Min useful IRQ percentage (1-100) */
	u32 coalesce_window_us;	/* Coalescing detection window */
};

/* Default thresholds for ARM Cortex-A series */
#define XSC_DB_DEFAULT_MAX_LATENCY_NS	500000	/* 500 µs */
#define XSC_DB_DEFAULT_P99_LATENCY_NS	150000	/* 150 µs */
#define XSC_DB_DEFAULT_MAX_SPURIOUS_PCT	1	/* 1% */
#define XSC_DB_DEFAULT_MIN_EFFECTIVE_PCT 95	/* 95% */
#define XSC_DB_DEFAULT_COALESCE_WINDOW	100	/* 100 µs */

/* Per-doorbell statistics (exposed via sysfs) */
struct xsc_doorbell_stats {
	atomic64_t total_irqs;		/* Total IRQs delivered */
	atomic64_t useful_irqs;		/* IRQs with actual work */
	atomic64_t spurious_irqs;	/* IRQs with no work */
	atomic64_t wrong_cpu_irqs;	/* IRQs on wrong CPU */
	atomic64_t poll_fallbacks;	/* Times we had to poll despite doorbell */
	atomic64_t coalesced_irqs;	/* IRQs that batched multiple requests */
	atomic64_t thermal_throttle;	/* Thermal events during operation */

	/* Latency tracking (in nanoseconds) */
	atomic64_t min_latency_ns;
	atomic64_t max_latency_ns;
	atomic64_t total_latency_ns;	/* For average calculation */

	/* Batch efficiency */
	atomic64_t total_sqes_processed; /* Total SQEs processed via IRQ */

	/* Power state transitions */
	atomic64_t idle_to_active_wakes; /* Wakes from deep idle */
	atomic64_t max_idle_wake_ns;	/* Worst idle wake latency */
};

/* Test payload for memory ordering validation */
struct xsc_doorbell_test_payload {
	atomic_t seq;			/* Sequence number */
	u64 timestamp;			/* TSC/CNTVCT timestamp */
	u8 data[128];			/* Test pattern */
	u32 checksum;			/* Simple validation */
} __aligned(128);

/* Doorbell device descriptor */
struct xsc_doorbell_device {
	/* Hardware resources */
	struct resource *mmio_res;	/* MMIO region */
	void __iomem *mmio_base;	/* Mapped doorbell register */
	int irq;			/* IRQ number */
	int target_cpu;			/* Affinity target */

	/* State tracking */
	enum xsc_doorbell_state state;
	enum xsc_doorbell_mode mode;

	/* Statistics & thresholds */
	struct xsc_doorbell_stats stats;
	struct xsc_doorbell_thresholds thresholds;

	/* Test infrastructure */
	struct xsc_doorbell_test_payload *test_payload;
	struct completion test_complete;
	atomic_t test_irq_count;

	/* Watchdog */
	struct delayed_work watchdog_work;
	atomic_t watchdog_failures;

	/* sysfs */
	struct kobject kobj;

	char name[32];			/* Device name for logging */
	char fail_reason[128];		/* Last failure reason */
};

/* Kernel API */
int xsc_doorbell_discover(void);
int xsc_doorbell_validate(struct xsc_doorbell_device *db);
void xsc_doorbell_enable(struct xsc_doorbell_device *db);
void xsc_doorbell_disable(struct xsc_doorbell_device *db);
void xsc_doorbell_rollback(struct xsc_doorbell_device *db, const char *reason);

/* Test functions */
int xsc_doorbell_preflight(struct xsc_doorbell_device *db);
int xsc_doorbell_self_test(struct xsc_doorbell_device *db);
int xsc_doorbell_soak_test(struct xsc_doorbell_device *db);
int xsc_doorbell_ordering_test(struct xsc_doorbell_device *db);
int xsc_doorbell_power_test(struct xsc_doorbell_device *db);
int xsc_doorbell_coalesce_test(struct xsc_doorbell_device *db);

/* Watchdog */
void xsc_doorbell_watchdog_init(struct xsc_doorbell_device *db);
void xsc_doorbell_watchdog_check(struct work_struct *work);
void xsc_doorbell_watchdog_stop(struct xsc_doorbell_device *db);

/* IRQ handler */
irqreturn_t xsc_doorbell_test_irq(int irq, void *data);

/* sysfs interface */
int xsc_doorbell_sysfs_init(struct xsc_doorbell_device *db);
void xsc_doorbell_sysfs_cleanup(struct xsc_doorbell_device *db);

/* Utility functions */
static inline u64 xsc_doorbell_get_timestamp(void)
{
#ifdef CONFIG_ARM64
	u64 val;
	asm volatile("mrs %0, cntvct_el0" : "=r" (val));
	return val;
#else
	return rdtsc();
#endif
}

static inline void xsc_doorbell_ring(struct xsc_doorbell_device *db, u32 ticket)
{
	/* Device-nGnRnE store ensures ordering */
	writel_relaxed(ticket, db->mmio_base);
	/* ARM64: DSB ensures visibility before SEV */
#ifdef CONFIG_ARM64
	asm volatile("dsb st" ::: "memory");
#else
	wmb();
#endif
}

static inline u64 xsc_doorbell_effectiveness(struct xsc_doorbell_stats *stats)
{
	u64 total = atomic64_read(&stats->total_irqs);
	u64 useful = atomic64_read(&stats->useful_irqs);

	return total ? (useful * 100) / total : 0;
}

static inline u64 xsc_doorbell_avg_latency(struct xsc_doorbell_stats *stats)
{
	u64 total_lat = atomic64_read(&stats->total_latency_ns);
	u64 count = atomic64_read(&stats->useful_irqs);

	return count ? total_lat / count : 0;
}

static inline u64 xsc_doorbell_avg_batch(struct xsc_doorbell_stats *stats)
{
	u64 sqes = atomic64_read(&stats->total_sqes_processed);
	u64 irqs = atomic64_read(&stats->useful_irqs);

	return irqs ? sqes / irqs : 0;
}

/* Configuration constants */
#define XSC_DB_SOAK_TEST_POKES		100000	/* Number of test doorbells */
#define XSC_DB_SOAK_MIN_INTERVAL_US	10	/* Min interval between pokes */
#define XSC_DB_SOAK_MAX_INTERVAL_US	50	/* Max interval */
#define XSC_DB_POWER_TEST_CYCLES	50	/* Power transition cycles */
#define XSC_DB_POWER_IDLE_MS		100	/* Idle duration per cycle */
#define XSC_DB_COALESCE_BURST_SIZE	10	/* Rapid pokes for coalesce test */
#define XSC_DB_WATCHDOG_INTERVAL_SEC	10	/* Watchdog check interval */
#define XSC_DB_MAX_WATCHDOG_FAILURES	3	/* Auto-rollback threshold */

#endif /* _XSC_DOORBELL_H */
