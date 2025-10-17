// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Doorbell Runtime Watchdog
 *
 * Continuously monitors doorbell health and triggers rollback on failures
 */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include "xsc_doorbell.h"

/*
 * Watchdog check function - runs periodically
 * Monitors for:
 * - IRQ starvation (no IRQs despite activity)
 * - Latency drift (degrading performance over time)
 * - Spurious IRQ rate increase
 * - Wrong CPU delivery
 */
void xsc_doorbell_watchdog_check(struct work_struct *work)
{
	struct xsc_doorbell_device *db = container_of(work,
						      struct xsc_doorbell_device,
						      watchdog_work.work);
	u64 total_irqs, spurious_irqs, wrong_cpu_irqs;
	u64 max_latency, avg_latency;
	u64 spurious_pct;
	bool failure = false;
	char reason[128] = {0};

	/* Don't run watchdog if not in steady state */
	if (db->state != XSC_DB_STEADY)
		return;

	/* Gather current stats */
	total_irqs = atomic64_read(&db->stats.total_irqs);
	spurious_irqs = atomic64_read(&db->stats.spurious_irqs);
	wrong_cpu_irqs = atomic64_read(&db->stats.wrong_cpu_irqs);
	max_latency = atomic64_read(&db->stats.max_latency_ns);
	avg_latency = xsc_doorbell_avg_latency(&db->stats);

	/* Check 1: Spurious IRQ rate */
	if (total_irqs > 1000) {
		spurious_pct = (spurious_irqs * 100) / total_irqs;
		if (spurious_pct > db->thresholds.max_spurious_pct) {
			snprintf(reason, sizeof(reason),
				 "Spurious IRQ rate %llu%% exceeds threshold %u%%",
				 spurious_pct, db->thresholds.max_spurious_pct);
			failure = true;
		}
	}

	/* Check 2: Wrong CPU delivery */
	if (wrong_cpu_irqs > 0) {
		u64 wrong_pct = (wrong_cpu_irqs * 100) / total_irqs;
		if (wrong_pct > 5) { /* More than 5% on wrong CPU */
			snprintf(reason, sizeof(reason),
				 "Wrong CPU IRQs: %llu (%llu%%)",
				 wrong_cpu_irqs, wrong_pct);
			failure = true;
		}
	}

	/* Check 3: Latency drift */
	if (max_latency > db->thresholds.max_latency_ns * 2) {
		snprintf(reason, sizeof(reason),
			 "Max latency %llu ns exceeds 2x threshold (%llu ns)",
			 max_latency, db->thresholds.max_latency_ns);
		failure = true;
	}

	/* Check 4: Average latency degradation */
	if (total_irqs > 1000 && avg_latency > db->thresholds.p99_latency_ns * 2) {
		snprintf(reason, sizeof(reason),
			 "Average latency %llu ns degraded (threshold: %llu ns)",
			 avg_latency, db->thresholds.p99_latency_ns);
		failure = true;
	}

	/* Check 5: Effectiveness (useful vs spurious) */
	if (total_irqs > 1000) {
		u64 effectiveness = xsc_doorbell_effectiveness(&db->stats);
		if (effectiveness < db->thresholds.min_effectiveness_pct) {
			snprintf(reason, sizeof(reason),
				 "Effectiveness %llu%% below threshold %u%%",
				 effectiveness, db->thresholds.min_effectiveness_pct);
			failure = true;
		}
	}

	if (failure) {
		int failures = atomic_inc_return(&db->watchdog_failures);

		pr_warn("xsc_doorbell: watchdog failure #%d for %s: %s\n",
			failures, db->name, reason);

		/* Auto-rollback after threshold failures */
		if (failures >= XSC_DB_MAX_WATCHDOG_FAILURES) {
			pr_err("xsc_doorbell: watchdog threshold reached, rolling back %s\n",
			       db->name);
			xsc_doorbell_rollback(db, reason);
			return; /* Don't reschedule */
		}
	} else {
		/* Reset failure counter on successful check */
		if (atomic_read(&db->watchdog_failures) > 0) {
			pr_info("xsc_doorbell: watchdog recovered for %s\n", db->name);
			atomic_set(&db->watchdog_failures, 0);
		}
	}

	/* Reschedule next check */
	schedule_delayed_work(&db->watchdog_work,
			      msecs_to_jiffies(XSC_DB_WATCHDOG_INTERVAL_SEC * 1000));
}

/*
 * Initialize watchdog for a doorbell device
 */
void xsc_doorbell_watchdog_init(struct xsc_doorbell_device *db)
{
	INIT_DELAYED_WORK(&db->watchdog_work, xsc_doorbell_watchdog_check);
	atomic_set(&db->watchdog_failures, 0);

	/* Start watchdog after initial validation */
	if (db->state == XSC_DB_STEADY) {
		pr_info("xsc_doorbell: starting watchdog for %s\n", db->name);
		schedule_delayed_work(&db->watchdog_work,
				      msecs_to_jiffies(XSC_DB_WATCHDOG_INTERVAL_SEC * 1000));
	}
}

/*
 * Stop watchdog (called during cleanup)
 */
void xsc_doorbell_watchdog_stop(struct xsc_doorbell_device *db)
{
	cancel_delayed_work_sync(&db->watchdog_work);
	pr_info("xsc_doorbell: watchdog stopped for %s\n", db->name);
}

MODULE_LICENSE("GPL");
