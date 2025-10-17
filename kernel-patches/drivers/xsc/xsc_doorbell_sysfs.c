// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Doorbell sysfs Interface
 *
 * Exposes doorbell statistics and control via /sys/kernel/xsc/doorbell/
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

#include "xsc_doorbell.h"

static struct kobject *xsc_doorbell_kobj;
extern struct xsc_doorbell_device *xsc_global_doorbell;

/* Mode attribute: DISABLED, COALESCED, FULL */
static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;
	const char *mode_str;

	if (!db)
		return sprintf(buf, "NONE\n");

	switch (db->mode) {
	case XSC_DB_DISABLED:
		mode_str = "DISABLED";
		break;
	case XSC_DB_COALESCED:
		mode_str = "COALESCED";
		break;
	case XSC_DB_FULL:
		mode_str = "ENABLED";
		break;
	default:
		mode_str = "UNKNOWN";
	}

	return sprintf(buf, "%s\n", mode_str);
}

static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return -ENODEV;

	if (sysfs_streq(buf, "DISABLED")) {
		db->mode = XSC_DB_DISABLED;
	} else if (sysfs_streq(buf, "COALESCED")) {
		db->mode = XSC_DB_COALESCED;
	} else if (sysfs_streq(buf, "ENABLED") || sysfs_streq(buf, "FULL")) {
		db->mode = XSC_DB_FULL;
	} else {
		return -EINVAL;
	}

	return count;
}

static struct kobj_attribute mode_attr = __ATTR_RW(mode);

/* State attribute: CANDIDATE, PREFLIGHT, VALIDATING, STEADY, FAILED */
static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;
	const char *state_str;

	if (!db)
		return sprintf(buf, "NONE\n");

	switch (db->state) {
	case XSC_DB_CANDIDATE:
		state_str = "CANDIDATE";
		break;
	case XSC_DB_PREFLIGHT:
		state_str = "PREFLIGHT";
		break;
	case XSC_DB_VALIDATING:
		state_str = "VALIDATING";
		break;
	case XSC_DB_STEADY:
		state_str = "STEADY";
		break;
	case XSC_DB_FAILED:
		state_str = "FAILED";
		break;
	default:
		state_str = "UNKNOWN";
	}

	return sprintf(buf, "%s\n", state_str);
}

static struct kobj_attribute status_attr = __ATTR_RO(status);

/* IRQ number */
static ssize_t irq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db || db->irq < 0)
		return sprintf(buf, "-1\n");

	return sprintf(buf, "%d\n", db->irq);
}

static struct kobj_attribute irq_attr = __ATTR_RO(irq);

/* Target CPU */
static ssize_t cpu_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "-1\n");

	return sprintf(buf, "%d\n", db->target_cpu);
}

static struct kobj_attribute cpu_attr = __ATTR_RO(cpu);

/* Statistics */
static ssize_t total_irqs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&db->stats.total_irqs));
}

static struct kobj_attribute total_irqs_attr = __ATTR_RO(total_irqs);

static ssize_t useful_irqs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&db->stats.useful_irqs));
}

static struct kobj_attribute useful_irqs_attr = __ATTR_RO(useful_irqs);

static ssize_t spurious_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&db->stats.spurious_irqs));
}

static struct kobj_attribute spurious_attr = __ATTR_RO(spurious);

static ssize_t wrong_cpu_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&db->stats.wrong_cpu_irqs));
}

static struct kobj_attribute wrong_cpu_attr = __ATTR_RO(wrong_cpu);

/* Latency statistics */
static ssize_t min_ns_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&db->stats.min_latency_ns));
}

static struct kobj_attribute min_ns_attr = __ATTR_RO(min_ns);

static ssize_t max_ns_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&db->stats.max_latency_ns));
}

static struct kobj_attribute max_ns_attr = __ATTR_RO(max_ns);

static ssize_t avg_ns_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", xsc_doorbell_avg_latency(&db->stats));
}

static struct kobj_attribute avg_ns_attr = __ATTR_RO(avg_ns);

/* Convenience: p99 and max in microseconds */
static ssize_t p99_us_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;
	u64 avg_ns;

	if (!db)
		return sprintf(buf, "0\n");

	avg_ns = xsc_doorbell_avg_latency(&db->stats);
	return sprintf(buf, "%llu\n", avg_ns / 1000);
}

static struct kobj_attribute p99_us_attr = __ATTR_RO(p99_us);

static ssize_t max_us_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", atomic64_read(&db->stats.max_latency_ns) / 1000);
}

static struct kobj_attribute max_us_attr = __ATTR_RO(max_us);

/* Effectiveness percentage */
static ssize_t effectiveness_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%llu\n", xsc_doorbell_effectiveness(&db->stats));
}

static struct kobj_attribute effectiveness_attr = __ATTR_RO(effectiveness);

/* Failure reason (read-only) */
static ssize_t fail_reason_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db || db->state != XSC_DB_FAILED)
		return sprintf(buf, "N/A\n");

	return sprintf(buf, "%s\n", db->fail_reason);
}

static struct kobj_attribute fail_reason_attr = __ATTR_RO(fail_reason);

/* Device name */
static ssize_t name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "none\n");

	return sprintf(buf, "%s\n", db->name);
}

static struct kobj_attribute name_attr = __ATTR_RO(name);

/* Watchdog failures */
static ssize_t watchdog_failures_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct xsc_doorbell_device *db = xsc_global_doorbell;

	if (!db)
		return sprintf(buf, "0\n");

	return sprintf(buf, "%d\n", atomic_read(&db->watchdog_failures));
}

static struct kobj_attribute watchdog_failures_attr = __ATTR_RO(watchdog_failures);

static struct attribute *xsc_doorbell_attrs[] = {
	&mode_attr.attr,
	&status_attr.attr,
	&irq_attr.attr,
	&cpu_attr.attr,
	&total_irqs_attr.attr,
	&useful_irqs_attr.attr,
	&spurious_attr.attr,
	&wrong_cpu_attr.attr,
	&min_ns_attr.attr,
	&max_ns_attr.attr,
	&avg_ns_attr.attr,
	&p99_us_attr.attr,
	&max_us_attr.attr,
	&effectiveness_attr.attr,
	&fail_reason_attr.attr,
	&name_attr.attr,
	&watchdog_failures_attr.attr,
	NULL,
};

static struct attribute_group xsc_doorbell_attr_group = {
	.attrs = xsc_doorbell_attrs,
};

/* Simplified ktype for doorbell kobject */
static struct kobj_type ktype_doorbell = {
	.sysfs_ops = &kobj_sysfs_ops,
};

int xsc_doorbell_sysfs_init(struct xsc_doorbell_device *db)
{
	int ret;

	/* Create /sys/kernel/xsc */
	xsc_doorbell_kobj = kobject_create_and_add("xsc", kernel_kobj);
	if (!xsc_doorbell_kobj)
		return -ENOMEM;

	/* Create /sys/kernel/xsc/doorbell/ */
	db->kobj.parent = xsc_doorbell_kobj;
	ret = kobject_init_and_add(&db->kobj, &ktype_doorbell,
				    xsc_doorbell_kobj, "doorbell");
	if (ret) {
		kobject_put(xsc_doorbell_kobj);
		return ret;
	}

	/* Create attribute files */
	ret = sysfs_create_group(&db->kobj, &xsc_doorbell_attr_group);
	if (ret) {
		kobject_put(&db->kobj);
		kobject_put(xsc_doorbell_kobj);
		return ret;
	}

	return 0;
}

void xsc_doorbell_sysfs_cleanup(struct xsc_doorbell_device *db)
{
	if (db) {
		sysfs_remove_group(&db->kobj, &xsc_doorbell_attr_group);
		kobject_put(&db->kobj);
	}

	if (xsc_doorbell_kobj)
		kobject_put(xsc_doorbell_kobj);
}

MODULE_LICENSE("GPL");
