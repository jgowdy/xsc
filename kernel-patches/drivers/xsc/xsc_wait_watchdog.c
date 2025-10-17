// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Wait Mechanism - Runtime Watchdog
 *
 * Monitors wait mechanism health and triggers rollback if needed
 */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include "../../include/xsc_wait.h"

/*
 * Watchdog check function (runs every 10 seconds)
 */
void xsc_wait_watchdog_check(struct work_struct *work)
{
	struct xsc_wait_mechanism *mech =
		container_of(work, struct xsc_wait_mechanism,
			     watchdog_work.work);
	u64 total, success, spurious;
	u64 success_rate, spurious_rate;
	u64 max_latency;
	bool should_rollback = false;
	char reason[128];

	/* Skip if already failed or degraded */
	if (mech->state == XSC_WAIT_FAILED ||
	    mech->state == XSC_WAIT_DEGRADED)
		goto reschedule;

	/* Read current stats */
	total = atomic64_read(&mech->stats.total_waits);
	success = atomic64_read(&mech->stats.successful_waits);
	spurious = atomic64_read(&mech->stats.spurious_wakes);
	max_latency = atomic64_read(&mech->stats.max_latency_ns);

	/* Skip if no activity yet */
	if (total < 100)
		goto reschedule;

	/* Calculate rates */
	success_rate = xsc_wait_success_rate(&mech->stats);
	spurious_rate = xsc_wait_spurious_rate(&mech->stats);

	/* Check success rate */
	if (success_rate < mech->thresholds.min_success_rate_pct) {
		snprintf(reason, sizeof(reason),
			 "Success rate degraded: %llu%% (threshold: %u%%)",
			 success_rate, mech->thresholds.min_success_rate_pct);
		should_rollback = true;
	}

	/* Check spurious wake rate */
	if (spurious_rate > mech->thresholds.max_spurious_pct) {
		snprintf(reason, sizeof(reason),
			 "Spurious wake rate too high: %llu%% (threshold: %u%%)",
			 spurious_rate, mech->thresholds.max_spurious_pct);
		should_rollback = true;
	}

	/* Check max latency */
	if (max_latency > mech->thresholds.max_latency_ns * 2) {
		snprintf(reason, sizeof(reason),
			 "Max latency exceeded: %llu ns (threshold: %llu ns)",
			 max_latency, mech->thresholds.max_latency_ns);
		should_rollback = true;
	}

	/* Trigger rollback if needed */
	if (should_rollback) {
		int failures = atomic_inc_return(&mech->watchdog_failures);

		pr_warn("xsc_wait: Watchdog failure #%d: %s\n", failures,
			reason);

		if (failures >= XSC_WAIT_MAX_WATCHDOG_FAILURES) {
			pr_err("xsc_wait: Maximum watchdog failures reached, rolling back\n");
			xsc_wait_rollback(mech, reason);
			/* Don't reschedule after rollback */
			return;
		}
	} else {
		/* Reset failure counter if things are working */
		atomic_set(&mech->watchdog_failures, 0);
	}

reschedule:
	/* Reschedule for next check */
	schedule_delayed_work(&mech->watchdog_work,
			      msecs_to_jiffies(
				      XSC_WAIT_WATCHDOG_INTERVAL_SEC * 1000));
}
EXPORT_SYMBOL_GPL(xsc_wait_watchdog_check);

/*
 * Initialize watchdog monitoring
 */
void xsc_wait_watchdog_init(struct xsc_wait_mechanism *mech)
{
	INIT_DELAYED_WORK(&mech->watchdog_work, xsc_wait_watchdog_check);
	atomic_set(&mech->watchdog_failures, 0);

	/* Start watchdog */
	schedule_delayed_work(&mech->watchdog_work,
			      msecs_to_jiffies(
				      XSC_WAIT_WATCHDOG_INTERVAL_SEC * 1000));

	pr_info("xsc_wait: Watchdog started (interval: %d seconds)\n",
		XSC_WAIT_WATCHDOG_INTERVAL_SEC);
}
EXPORT_SYMBOL_GPL(xsc_wait_watchdog_init);

/*
 * Stop watchdog monitoring
 */
void xsc_wait_watchdog_stop(struct xsc_wait_mechanism *mech)
{
	cancel_delayed_work_sync(&mech->watchdog_work);
	pr_info("xsc_wait: Watchdog stopped\n");
}
EXPORT_SYMBOL_GPL(xsc_wait_watchdog_stop);

MODULE_LICENSE("GPL");
