/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CFI JIT Allowlist
 *
 * Controls hardware Control-Flow Integrity enforcement for JIT engines.
 *
 * JIT engines (Java, Node.js, LuaJIT) generate code at runtime and struggle
 * with hardware CFI requirements:
 * - Intel CET: ENDBR64 landing pads for indirect branches
 * - ARM PAC: Pointer authentication for return addresses
 *
 * This allowlist allows specific binaries to run with CFI disabled.
 * ALL other processes have full CFI enforcement enabled.
 */

#ifndef _CFI_ALLOWLIST_H
#define _CFI_ALLOWLIST_H

#include <linux/types.h>

/*
 * CFI enforcement modes
 *
 * CFI_MODE_ENFORCED is 0 so zeroing the flag keeps you in secure mode.
 * Set once at exec time based on binary allowlist (if CONFIG_CFI_JIT_ALLOWLIST=y).
 */
enum cfi_mode {
	CFI_MODE_ENFORCED = 0,	/* Full CFI enforcement (default, secure) */
#ifdef CONFIG_CFI_JIT_ALLOWLIST
	CFI_MODE_DISABLED,	/* CFI disabled (allowlisted JIT engines only) */
#endif
};

#ifdef CONFIG_CFI_JIT_ALLOWLIST
/*
 * Runtime allowlist control - READ-ONLY after boot
 *
 * If allowlist is empty, this is false -> behaves like hard enforcement.
 * Protects against kernel write primitives: even if attacker flips
 * task->cfi_mode, this global flag forces enforcement.
 */
extern bool cfi_allowlist_active;

/*
 * Check if binary is allowlisted for CFI exemption
 *
 * Called once at exec time to determine CFI mode.
 */
bool cfi_is_binary_allowlisted(const char *pathname, struct file *file);

/*
 * Handle exec - check allowlist and set CFI mode
 *
 * Called at exec time to determine and apply CFI enforcement.
 * Mode is determined once based on binary allowlist and set for process lifetime.
 */
int cfi_allowlist_exec(struct task_struct *task, const char *pathname, struct file *file);

/*
 * Handle fork - inherit parent's CFI mode
 */
void cfi_allowlist_fork(struct task_struct *parent, struct task_struct *child);

/*
 * Get CFI mode for task
 */
static inline enum cfi_mode cfi_get_mode(struct task_struct *task)
{
	return task->cfi_mode;
}

/*
 * Check if CFI should be enforced for this task
 *
 * Returns true if CFI should be enforced (default, secure).
 * Returns false only for allowlisted JITs.
 */
static inline bool cfi_is_enforced(struct task_struct *task)
{
	return task->cfi_mode == CFI_MODE_ENFORCED;
}

#else /* !CONFIG_CFI_JIT_ALLOWLIST */
/*
 * JIT allowlist disabled - all processes have CFI enforced
 */
static inline int cfi_allowlist_exec(struct task_struct *task, const char *pathname, struct file *file)
{
	return 0; /* No-op: CFI always enforced */
}

static inline void cfi_allowlist_fork(struct task_struct *parent, struct task_struct *child)
{
	/* No-op: CFI always enforced */
}

static inline bool cfi_is_enforced(struct task_struct *task)
{
	return true; /* Always enforce CFI */
}
#endif /* CONFIG_CFI_JIT_ALLOWLIST */

/*
 * Initialize CFI mode for new task (always needed)
 */
void cfi_allowlist_init_task(struct task_struct *task);

/*
 * Log CFI violation (when JIT attempts to violate CFI)
 */
void cfi_allowlist_violation(struct task_struct *task, const char *violation_type);

#endif /* _CFI_ALLOWLIST_H */
