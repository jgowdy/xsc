// SPDX-License-Identifier: GPL-2.0
/*
 * CFI JIT Allowlist
 *
 * Controls hardware Control-Flow Integrity enforcement for JIT engines.
 * Allowlist is loaded from /etc/cfi/allowlist at boot (via initramfs).
 * Allowlist is checked once at exec and CFI mode stored in task_struct.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/uaccess.h>

#include "../include/cfi_allowlist.h"

#ifdef CONFIG_CFI_JIT_ALLOWLIST
/*
 * Binary Allowlist - loaded from /etc/cfi/allowlist at boot
 *
 * ONLY for JIT engines that cannot comply with hardware CFI:
 * - Intel CET (ENDBR64 indirect branch tracking, shadow stack)
 * - ARM PAC (pointer authentication codes)
 *
 * This list is loaded from initramfs at boot time and stored in
 * kernel memory protected by __ro_after_init.
 *
 * Package managers update /etc/cfi/allowlist and regenerate initramfs
 * when JIT packages are installed/removed.
 *
 * System components and regular applications ALL have CFI enforced.
 */
#define CFI_ALLOWLIST_PATH "/etc/cfi/allowlist"
#define CFI_MAX_ALLOWLIST_ENTRIES 64
#define CFI_MAX_PATH_LENGTH 256

/*
 * Dynamically loaded allowlist - READ-ONLY after boot
 *
 * Loaded from /etc/cfi/allowlist during subsys_initcall.
 * Protected by __ro_after_init after kernel init completes.
 */
static char **cfi_binary_allowlist __ro_after_init;
static int cfi_allowlist_count __ro_after_init;

/*
 * Runtime allowlist control - READ-ONLY after boot
 *
 * Defense-in-depth: Even if CONFIG_CFI_JIT_ALLOWLIST=y, an empty
 * allowlist means this is false -> behaves like hard enforcement.
 *
 * Protection against kernel write exploits:
 * - __ro_after_init makes this READ-ONLY after kernel init completes
 * - Even if attacker flips task->cfi_mode via kernel write,
 *   they CANNOT flip this flag (it's in read-only memory)
 * - Would require modifying page tables to make writable (harder exploit)
 *
 * Set once at boot based on allowlist contents, then immutable.
 */
bool cfi_allowlist_active __ro_after_init = false;
EXPORT_SYMBOL_GPL(cfi_allowlist_active);

/*
 * Check if binary is allowlisted for CFI exemption
 *
 * Called once at exec time.
 * Checks against dynamically loaded allowlist from /etc/cfi/allowlist.
 */
bool cfi_is_binary_allowlisted(const char *pathname, struct file *file)
{
	int i;
	char *path_buf;
	char *full_path;
	bool allowed = false;

	/* If allowlist not loaded or empty, enforce CFI everywhere */
	if (!cfi_binary_allowlist || cfi_allowlist_count == 0)
		return false;

	path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!path_buf)
		return false;

	full_path = d_path(&file->f_path, path_buf, PATH_MAX);
	if (IS_ERR(full_path)) {
		kfree(path_buf);
		return false;
	}

	/* Check against loaded allowlist */
	for (i = 0; i < cfi_allowlist_count; i++) {
		if (strcmp(full_path, cfi_binary_allowlist[i]) == 0) {
			allowed = true;
			pr_info("cfi_allowlist: CFI DISABLED for JIT: %s\n", full_path);
			break;
		}
	}

	if (!allowed) {
		pr_debug("cfi_allowlist: CFI ENFORCED for: %s\n", full_path);
	}

	kfree(path_buf);
	return allowed;
}
EXPORT_SYMBOL_GPL(cfi_is_binary_allowlisted);

/*
 * Handle exec - determine and set CFI mode
 *
 * Called at exec time. CFI mode is determined once based on
 * binary allowlist and set immutably for process lifetime.
 *
 * Subsequent execs will re-evaluate the new binary and set new mode.
 */
int cfi_allowlist_exec(struct task_struct *task, const char *pathname, struct file *file)
{
	enum cfi_mode new_mode;

	/* Determine CFI mode based on allowlist (checked once) */
	if (cfi_is_binary_allowlisted(pathname, file))
		new_mode = CFI_MODE_DISABLED;  /* JIT: disable CFI */
	else
		new_mode = CFI_MODE_ENFORCED;  /* Normal: enforce CFI */

	/* Set CFI mode for this binary */
	task->cfi_mode = new_mode;

	pr_debug("cfi_allowlist: Process %d (%s): CFI mode %s\n",
		task->pid, task->comm,
		new_mode == CFI_MODE_ENFORCED ? "ENFORCED" : "DISABLED");

	return 0;
}
EXPORT_SYMBOL_GPL(cfi_allowlist_exec);

/*
 * Handle fork - inherit parent's CFI mode
 */
void cfi_allowlist_fork(struct task_struct *parent, struct task_struct *child)
{
	child->cfi_mode = parent->cfi_mode;
}
EXPORT_SYMBOL_GPL(cfi_allowlist_fork);
#endif /* CONFIG_CFI_JIT_ALLOWLIST */

/*
 * Initialize CFI mode for new task
 */
void cfi_allowlist_init_task(struct task_struct *task)
{
#ifdef CONFIG_CFI_JIT_ALLOWLIST
	/* Set default mode to CFI_MODE_ENFORCED (0) */
	task->cfi_mode = CFI_MODE_ENFORCED;
#else
	/* No-op: all processes always have CFI enforced, no field needed */
#endif
}
EXPORT_SYMBOL_GPL(cfi_allowlist_init_task);

/*
 * Log CFI violation
 */
void cfi_allowlist_violation(struct task_struct *task, const char *violation_type)
{
	pr_warn("cfi_allowlist: VIOLATION - Process %d (%s) attempted: %s (CFI mode %d)\n",
		task->pid, task->comm, violation_type, task->cfi_mode);
}
EXPORT_SYMBOL_GPL(cfi_allowlist_violation);

#ifdef CONFIG_CFI_JIT_ALLOWLIST
/*
 * Load allowlist from /etc/cfi/allowlist
 *
 * Format: One absolute path per line, empty lines and # comments ignored.
 * Called during subsys_initcall before __ro_after_init protection.
 */
static int __init cfi_load_allowlist(void)
{
	struct file *file;
	loff_t pos = 0;
	char *buf;
	char *line_start, *line_end;
	int count = 0;
	ssize_t bytes_read;

	/* Allocate temporary buffer for file reading */
	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Allocate allowlist array (will be protected by __ro_after_init) */
	cfi_binary_allowlist = kzalloc(sizeof(char *) * CFI_MAX_ALLOWLIST_ENTRIES, GFP_KERNEL);
	if (!cfi_binary_allowlist) {
		kfree(buf);
		return -ENOMEM;
	}

	/* Open allowlist file from initramfs */
	file = filp_open(CFI_ALLOWLIST_PATH, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_info("cfi_allowlist: No allowlist file at %s (CFI enforced for all processes)\n",
			CFI_ALLOWLIST_PATH);
		kfree(buf);
		return 0; /* Not an error - empty allowlist is valid */
	}

	pr_info("cfi_allowlist: Loading JIT allowlist from %s\n", CFI_ALLOWLIST_PATH);

	/* Read file in chunks */
	while (count < CFI_MAX_ALLOWLIST_ENTRIES) {
		bytes_read = kernel_read(file, buf, PAGE_SIZE - 1, &pos);
		if (bytes_read <= 0)
			break;

		buf[bytes_read] = '\0';
		line_start = buf;

		/* Process each line */
		while ((line_end = strchr(line_start, '\n')) != NULL) {
			*line_end = '\0';

			/* Skip empty lines and comments */
			while (*line_start == ' ' || *line_start == '\t')
				line_start++;

			if (*line_start == '\0' || *line_start == '#') {
				line_start = line_end + 1;
				continue;
			}

			/* Validate absolute path */
			if (*line_start != '/') {
				pr_warn("cfi_allowlist: Ignoring invalid path (not absolute): %s\n",
					line_start);
				line_start = line_end + 1;
				continue;
			}

			/* Allocate and store path */
			cfi_binary_allowlist[count] = kstrdup(line_start, GFP_KERNEL);
			if (!cfi_binary_allowlist[count]) {
				pr_err("cfi_allowlist: Failed to allocate memory for allowlist entry\n");
				break;
			}

			pr_info("cfi_allowlist: [%d] %s (CFI disabled for this JIT)\n",
				count, cfi_binary_allowlist[count]);
			count++;

			if (count >= CFI_MAX_ALLOWLIST_ENTRIES) {
				pr_warn("cfi_allowlist: Maximum allowlist entries (%d) reached\n",
					CFI_MAX_ALLOWLIST_ENTRIES);
				break;
			}

			line_start = line_end + 1;
		}
	}

	filp_close(file, NULL);
	kfree(buf);

	cfi_allowlist_count = count;
	return 0;
}

/*
 * Initialize CFI allowlist at boot
 *
 * Loads allowlist from /etc/cfi/allowlist (embedded in initramfs).
 * After this function completes, __ro_after_init makes the allowlist
 * and cfi_allowlist_active flag immutable.
 */
static int __init cfi_allowlist_init(void)
{
	int ret;

	/* Load allowlist from /etc/cfi/allowlist */
	ret = cfi_load_allowlist();
	if (ret < 0) {
		pr_err("cfi_allowlist: Failed to load allowlist: %d\n", ret);
		/* Continue with empty allowlist (full CFI enforcement) */
	}

	/* Check if allowlist has any entries */
	if (cfi_allowlist_count > 0) {
		cfi_allowlist_active = true;
		pr_info("cfi_allowlist: JIT allowlist ACTIVE (%d JIT engines with CFI disabled)\n",
			cfi_allowlist_count);
		pr_info("cfi_allowlist: All other processes have FULL CFI enforcement\n");
	} else {
		cfi_allowlist_active = false;
		pr_info("cfi_allowlist: JIT allowlist EMPTY - FULL CFI enforcement for all processes\n");
	}

	return 0;
}

subsys_initcall(cfi_allowlist_init);
#endif /* CONFIG_CFI_JIT_ALLOWLIST */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XSC Development Team");
MODULE_DESCRIPTION("CFI JIT allowlist - control hardware CFI enforcement for JIT engines");
