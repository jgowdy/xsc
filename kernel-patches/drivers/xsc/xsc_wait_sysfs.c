// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Wait Mechanism - sysfs Interface
 *
 * Exposes wait mechanism statistics and configuration via sysfs
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "../../include/xsc_wait.h"

/* Kobject for /sys/kernel/xsc_wait/ */
static struct kobject *xsc_wait_kobj = NULL;

/* Show wait mechanism type */
static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "none\n");

	switch (mech->primary) {
	case XSC_WAIT_UMWAIT:
		return sprintf(buf, "UMWAIT\n");
	case XSC_WAIT_PAUSE:
		return sprintf(buf, "PAUSE\n");
	case XSC_WAIT_WFE:
		return sprintf(buf, "WFE\n");
	case XSC_WAIT_DOORBELL:
		return sprintf(buf, "DOORBELL\n");
	case XSC_WAIT_GIC_LPI:
		return sprintf(buf, "GIC_LPI\n");
	case XSC_WAIT_FUTEX:
		return sprintf(buf, "FUTEX\n");
	default:
		return sprintf(buf, "unknown\n");
	}
}
static struct kobj_attribute type_attr = __ATTR_RO(type);

/* Show wait mechanism state */
static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "none\n");

	switch (mech->state) {
	case XSC_WAIT_CANDIDATE:
		return sprintf(buf, "CANDIDATE\n");
	case XSC_WAIT_VALIDATING:
		return sprintf(buf, "VALIDATING\n");
	case XSC_WAIT_ACTIVE:
		return sprintf(buf, "ACTIVE\n");
	case XSC_WAIT_DEGRADED:
		return sprintf(buf, "DEGRADED\n");
	case XSC_WAIT_FAILED:
		return sprintf(buf, "FAILED\n");
	default:
		return sprintf(buf, "unknown\n");
	}
}
static struct kobj_attribute state_attr = __ATTR_RO(state);

/* Show mechanism name */
static ssize_t name_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "none\n");

	return sprintf(buf, "%s\n", mech->name);
}
static struct kobj_attribute name_attr = __ATTR_RO(name);

/* Show total waits */
static ssize_t total_waits_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n",
		       atomic64_read(&mech->stats.total_waits));
}
static struct kobj_attribute total_waits_attr = __ATTR_RO(total_waits);

/* Show successful waits */
static ssize_t successful_waits_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n",
		       atomic64_read(&mech->stats.successful_waits));
}
static struct kobj_attribute successful_waits_attr =
	__ATTR_RO(successful_waits);

/* Show timeouts */
static ssize_t timeouts_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&mech->stats.timeouts));
}
static struct kobj_attribute timeouts_attr = __ATTR_RO(timeouts);

/* Show spurious wakes */
static ssize_t spurious_wakes_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n",
		       atomic64_read(&mech->stats.spurious_wakes));
}
static struct kobj_attribute spurious_wakes_attr = __ATTR_RO(spurious_wakes);

/* Show min latency */
static ssize_t min_latency_ns_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n",
		       atomic64_read(&mech->stats.min_latency_ns));
}
static struct kobj_attribute min_latency_ns_attr = __ATTR_RO(min_latency_ns);

/* Show max latency */
static ssize_t max_latency_ns_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n",
		       atomic64_read(&mech->stats.max_latency_ns));
}
static struct kobj_attribute max_latency_ns_attr = __ATTR_RO(max_latency_ns);

/* Show average latency */
static ssize_t avg_latency_ns_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", xsc_wait_avg_latency(&mech->stats));
}
static struct kobj_attribute avg_latency_ns_attr = __ATTR_RO(avg_latency_ns);

/* Show success rate percentage */
static ssize_t success_rate_pct_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", xsc_wait_success_rate(&mech->stats));
}
static struct kobj_attribute success_rate_pct_attr =
	__ATTR_RO(success_rate_pct);

/* Show deep sleeps */
static ssize_t deep_sleeps_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n",
		       atomic64_read(&mech->stats.deep_sleeps));
}
static struct kobj_attribute deep_sleeps_attr = __ATTR_RO(deep_sleeps);

/* Show shallow spins */
static ssize_t shallow_spins_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n",
		       atomic64_read(&mech->stats.shallow_spins));
}
static struct kobj_attribute shallow_spins_attr = __ATTR_RO(shallow_spins);

/* Show failure reason */
static ssize_t fail_reason_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct xsc_wait_mechanism *mech = xsc_global_wait;

	if (!mech)
		return sprintf(buf, "none\n");

	if (mech->fail_reason[0])
		return sprintf(buf, "%s\n", mech->fail_reason);
	else
		return sprintf(buf, "none\n");
}
static struct kobj_attribute fail_reason_attr = __ATTR_RO(fail_reason);

/* Attribute list */
static struct attribute *xsc_wait_attrs[] = {
	&type_attr.attr,
	&state_attr.attr,
	&name_attr.attr,
	&total_waits_attr.attr,
	&successful_waits_attr.attr,
	&timeouts_attr.attr,
	&spurious_wakes_attr.attr,
	&min_latency_ns_attr.attr,
	&max_latency_ns_attr.attr,
	&avg_latency_ns_attr.attr,
	&success_rate_pct_attr.attr,
	&deep_sleeps_attr.attr,
	&shallow_spins_attr.attr,
	&fail_reason_attr.attr,
	NULL,
};

static struct attribute_group xsc_wait_attr_group = {
	.attrs = xsc_wait_attrs,
};

/*
 * Initialize sysfs interface
 */
int xsc_wait_sysfs_init(struct xsc_wait_mechanism *mech)
{
	int ret;

	/* Create /sys/kernel/xsc_wait/ */
	xsc_wait_kobj = kobject_create_and_add("xsc_wait", kernel_kobj);
	if (!xsc_wait_kobj)
		return -ENOMEM;

	/* Create attribute files */
	ret = sysfs_create_group(xsc_wait_kobj, &xsc_wait_attr_group);
	if (ret) {
		kobject_put(xsc_wait_kobj);
		xsc_wait_kobj = NULL;
		return ret;
	}

	pr_info("xsc_wait: sysfs interface created at /sys/kernel/xsc_wait/\n");
	return 0;
}
EXPORT_SYMBOL_GPL(xsc_wait_sysfs_init);

/*
 * Cleanup sysfs interface
 */
void xsc_wait_sysfs_cleanup(struct xsc_wait_mechanism *mech)
{
	if (xsc_wait_kobj) {
		sysfs_remove_group(xsc_wait_kobj, &xsc_wait_attr_group);
		kobject_put(xsc_wait_kobj);
		xsc_wait_kobj = NULL;
	}
}
EXPORT_SYMBOL_GPL(xsc_wait_sysfs_cleanup);

MODULE_LICENSE("GPL");
