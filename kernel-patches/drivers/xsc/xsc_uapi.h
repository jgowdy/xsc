/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * XSC (eXtended Syscall) UAPI - Userspace API definitions
 * Copyright (C) 2025
 */

#ifndef _UAPI_LINUX_XSC_H
#define _UAPI_LINUX_XSC_H

#include <linux/types.h>

/*
 * XSC Ring Operation Types
 */
#define XSC_OP_NOP		0
#define XSC_OP_READ		1
#define XSC_OP_WRITE		2
#define XSC_OP_OPEN		3
#define XSC_OP_CLOSE		4
#define XSC_OP_FSYNC		5
#define XSC_OP_READV		6
#define XSC_OP_WRITEV		7
#define XSC_OP_PREAD		8
#define XSC_OP_PWRITE		9
#define XSC_OP_SENDTO		10
#define XSC_OP_RECVFROM		11
#define XSC_OP_ACCEPT		12
#define XSC_OP_CONNECT		13
#define XSC_OP_POLL		14
#define XSC_OP_EPOLL_WAIT	15
#define XSC_OP_SELECT		16
#define XSC_OP_NANOSLEEP	17
#define XSC_OP_CLOCK_NANOSLEEP	18
#define XSC_OP_FUTEX_WAIT	19
#define XSC_OP_FUTEX_WAKE	20
#define XSC_OP_FORK		21
#define XSC_OP_VFORK		22
#define XSC_OP_CLONE		23
#define XSC_OP_EXECVE		24
#define XSC_OP_EXECVEAT		25
#define XSC_OP_STAT		26
#define XSC_OP_FSTAT		27
#define XSC_OP_LSTAT		28
#define XSC_OP_SOCKET		29
#define XSC_OP_BIND		30
#define XSC_OP_LISTEN		31

/*
 * XSC Flags
 */
#define XSC_F_LINK		(1U << 0)	/* Link next operation */
#define XSC_F_DRAIN		(1U << 1)	/* Drain prior ops */
#define XSC_F_IOSQE_ASYNC	(1U << 2)	/* Force async */
#define XSC_F_FIXED_FILE	(1U << 3)	/* Fixed file descriptor */

/*
 * Submission Queue Entry (SQE)
 */
struct xsc_sqe {
	__u8	opcode;		/* XSC_OP_* */
	__u8	flags;		/* XSC_F_* */
	__u16	ioprio;		/* I/O priority */
	__s32	fd;		/* File descriptor */
	union {
		__u64	off;	/* Offset into file */
		__u64	addr2;	/* Secondary address */
	};
	union {
		__u64	addr;	/* Pointer to buffer or args */
		__u64	splice_off_in;
	};
	__u32	len;		/* Buffer size or arg count */
	union {
		__kernel_rwf_t	rw_flags;
		__u32		fsync_flags;
		__u16		poll_events;
		__u32		poll32_events;
		__u32		sync_range_flags;
		__u32		msg_flags;
		__u32		timeout_flags;
		__u32		accept_flags;
		__u32		cancel_flags;
		__u32		open_flags;
		__u32		statx_flags;
		__u32		fadvise_advice;
		__u32		splice_flags;
	};
	__u64	user_data;	/* Passed back at completion */
	union {
		__u16	buf_index;
		__u16	buf_group;
	} __attribute__((packed));
	__u16	personality;
	union {
		__s32	splice_fd_in;
		__u32	file_index;
	};
	__u64	__pad2[2];
};

/*
 * Completion Queue Entry (CQE)
 */
struct xsc_cqe {
	__u64	user_data;	/* SQE user_data */
	__s32	res;		/* Result code */
	__u32	flags;		/* Completion flags */
};

/*
 * XSC Device Setup Structures
 */
struct xsc_params {
	__u32	sq_entries;
	__u32	cq_entries;
	__u32	flags;
	__u32	sq_thread_cpu;
	__u32	sq_thread_idle;
	__u32	features;
	__u32	wq_fd;
	__u32	resv[3];
	struct xsc_sqe_ring sq_off;
	struct xsc_cqe_ring cq_off;
};

struct xsc_sqe_ring {
	__u32	head;
	__u32	tail;
	__u32	ring_mask;
	__u32	ring_entries;
	__u32	flags;
	__u32	dropped;
	__u32	array;
	__u32	resv1;
	__u64	resv2;
};

struct xsc_cqe_ring {
	__u32	head;
	__u32	tail;
	__u32	ring_mask;
	__u32	ring_entries;
	__u32	overflow;
	__u32	cqes;
	__u64	resv[2];
};

/*
 * IOCTLs
 */
#define XSC_IOC_MAGIC		'x'
#define XSC_IOC_SETUP		_IOWR(XSC_IOC_MAGIC, 0, struct xsc_params)
#define XSC_IOC_REGISTER_FILES	_IOW(XSC_IOC_MAGIC, 1, struct xsc_files_update)
#define XSC_IOC_UNREGISTER_FILES _IO(XSC_IOC_MAGIC, 2)

struct xsc_files_update {
	__u32	offset;
	__u32	resv;
	__aligned_u64 fds;
};

/*
 * ELF Note for XSC ABI version
 */
#define XSC_ABI_NOTE_NAME	"XSC"
#define XSC_ABI_NOTE_TYPE	1
#define XSC_ABI_VERSION		1

#endif /* _UAPI_LINUX_XSC_H */
