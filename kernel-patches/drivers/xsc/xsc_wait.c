// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Cross-Architecture Wait Mechanism - Core Implementation
 *
 * Unified wait mechanism with runtime platform detection
 * and automatic selection of optimal waiting strategy.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/of.h>

#include "../../include/xsc_wait.h"

/* Global wait mechanism (initialized at module load) */
struct xsc_wait_mechanism *xsc_global_wait = NULL;
EXPORT_SYMBOL_GPL(xsc_global_wait);

/* TSC/counter frequency for cycle conversion */
u64 xsc_tsc_freq_ghz = 0;
EXPORT_SYMBOL_GPL(xsc_tsc_freq_ghz);

/*
 * Calibrate TSC/counter frequency
 */
static u64 calibrate_tsc_freq(void)
{
#ifdef CONFIG_X86_64
	/* Use CPU frequency from cpufreq or tsc_khz */
	if (cpu_khz)
		return cpu_khz / 1000;  /* Convert kHz to MHz, then treat as GHz in calculations */

	/* Fallback: assume 2.5 GHz */
	return 2500;
#elif defined(CONFIG_ARM64)
	/* ARM64: Read CNTFRQ_EL0 */
	u64 freq;
	asm volatile("mrs %0, cntfrq_el0" : "=r" (freq));

	/* Convert Hz to GHz equivalent for our cycle calculations */
	return freq / 1000000;  /* MHz */
#else
	return 2000;  /* Generic fallback: 2 GHz */
#endif
}

/*
 * Initialize wait mechanism subsystem
 */
int xsc_wait_init(void)
{
	int ret;

	pr_info("xsc_wait: Initializing wait mechanisms\n");

	/* Allocate global mechanism descriptor */
	xsc_global_wait = kzalloc(sizeof(*xsc_global_wait), GFP_KERNEL);
	if (!xsc_global_wait)
		return -ENOMEM;

	/* Initialize completion for GIC wait (ARM64 only) */
#ifdef CONFIG_ARM64
	init_completion(&xsc_global_wait->gic_wait_complete);
#endif

	/* Calibrate TSC frequency */
	xsc_tsc_freq_ghz = calibrate_tsc_freq();
	pr_info("xsc_wait: Calibrated frequency: %llu MHz\n", xsc_tsc_freq_ghz);

	/* Platform-specific detection */
#ifdef CONFIG_X86_64
	ret = xsc_wait_detect_x86(xsc_global_wait);
	if (ret) {
		pr_err("xsc_wait: x86-64 detection failed: %d\n", ret);
		goto cleanup;
	}
#elif defined(CONFIG_ARM64)
	ret = xsc_wait_detect_arm64(xsc_global_wait);
	if (ret) {
		pr_err("xsc_wait: ARM64 detection failed: %d\n", ret);
		goto cleanup;
	}
#else
	pr_err("xsc_wait: Unsupported architecture\n");
	ret = -ENOTSUP;
	goto cleanup;
#endif

	/* Run validation */
	ret = xsc_wait_validate(xsc_global_wait);
	if (ret) {
		pr_warn("xsc_wait: Validation failed, using degraded mode\n");
		/* Not fatal - mechanism will use fallback */
	}

	/* Initialize sysfs interface */
	ret = xsc_wait_sysfs_init(xsc_global_wait);
	if (ret)
		pr_warn("xsc_wait: sysfs init failed (non-fatal): %d\n", ret);

	/* Start watchdog monitoring */
	xsc_wait_watchdog_init(xsc_global_wait);

	pr_info("xsc_wait: Initialized successfully\n");
	pr_info("xsc_wait:   Mechanism: %s\n", xsc_global_wait->name);
	pr_info("xsc_wait:   Primary: %d, Fallback: %d\n",
		xsc_global_wait->primary, xsc_global_wait->fallback);
	pr_info("xsc_wait:   State: %d\n", xsc_global_wait->state);

	return 0;

cleanup:
	kfree(xsc_global_wait);
	xsc_global_wait = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(xsc_wait_init);

/*
 * Run platform-specific validation
 */
int xsc_wait_validate(struct xsc_wait_mechanism *mech)
{
#ifdef CONFIG_X86_64
	return xsc_wait_validate_x86(mech);
#elif defined(CONFIG_ARM64)
	return xsc_wait_validate_arm64(mech);
#else
	return -ENOTSUP;
#endif
}
EXPORT_SYMBOL_GPL(xsc_wait_validate);

/*
 * Rollback to safe fallback mechanism
 */
void xsc_wait_rollback(struct xsc_wait_mechanism *mech, const char *reason)
{
	pr_warn("xsc_wait: ROLLBACK triggered: %s\n", reason);

	/* Copy failure reason */
	strscpy(mech->fail_reason, reason, sizeof(mech->fail_reason));

	/* Disable failed primary mechanism */
	switch (mech->primary) {
#ifdef CONFIG_X86_64
	case XSC_WAIT_UMWAIT:
		pr_warn("xsc_wait: Disabling UMWAIT, falling back to PAUSE\n");
		mech->has_umwait = false;
		mech->primary = XSC_WAIT_PAUSE;
		break;
#endif

#ifdef CONFIG_ARM64
	case XSC_WAIT_GIC_LPI:
		pr_warn("xsc_wait: Disabling GIC LPI, falling back to WFE\n");
		mech->has_gic_lpi = false;
		mech->primary = XSC_WAIT_WFE;
		break;

	case XSC_WAIT_DOORBELL:
		pr_warn("xsc_wait: Disabling doorbell, falling back to WFE\n");
		mech->has_doorbell = false;
		mech->primary = XSC_WAIT_WFE;
		break;
#endif

	default:
		pr_warn("xsc_wait: Already using fallback mechanism\n");
		break;
	}

	/* Mark as degraded */
	mech->state = XSC_WAIT_DEGRADED;
	atomic_inc(&mech->watchdog_failures);
}
EXPORT_SYMBOL_GPL(xsc_wait_rollback);

/*
 * Cleanup wait mechanism subsystem
 */
void xsc_wait_cleanup(void)
{
	if (!xsc_global_wait)
		return;

	pr_info("xsc_wait: Shutting down\n");

	/* Stop watchdog */
	xsc_wait_watchdog_stop(xsc_global_wait);

	/* Cleanup sysfs */
	xsc_wait_sysfs_cleanup(xsc_global_wait);

	/* Platform-specific cleanup */
#ifdef CONFIG_ARM64
	xsc_wait_cleanup_arm64();
#endif

	/* Free global structure */
	kfree(xsc_global_wait);
	xsc_global_wait = NULL;

	pr_info("xsc_wait: Shutdown complete\n");
}
EXPORT_SYMBOL_GPL(xsc_wait_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XSC Development Team");
MODULE_DESCRIPTION("Cross-architecture optimal wait mechanisms");
