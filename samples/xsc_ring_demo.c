// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal userspace demo for the XSC ring interface.
 * Matches the v8 UAPI (see kernel-patches/drivers/xsc/xsc_uapi.h).
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../kernel-patches/drivers/xsc/xsc_uapi.h"

static void fatal(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

int main(void)
{
	int xsc_fd, fd;
	struct xsc_params params;
	struct xsc_sqe *sqes;
	struct xsc_cqe *cqes;
	struct xsc_sqe *sqe;
	struct xsc_cqe *cqe;
	uint32_t *sq_head, *sq_tail, *sq_mask;
	uint32_t *cq_head, *cq_tail, *cq_mask;
	void *sq_ring, *cq_ring;
	char buf[64];
	int ret;

	xsc_fd = open("/dev/xsc", O_RDWR);
	if (xsc_fd < 0)
		fatal("open /dev/xsc");

	memset(&params, 0, sizeof(params));
	params.sq_entries = 64;
	params.cq_entries = 64;

	ret = ioctl(xsc_fd, XSC_IOC_SETUP, &params);
	if (ret < 0)
		fatal("XSC_IOC_SETUP");

	sq_ring = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd, 0);
	if (sq_ring == MAP_FAILED)
		fatal("mmap sq_ring");

	cq_ring = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
			 xsc_fd, 0x10000000);
	if (cq_ring == MAP_FAILED)
		fatal("mmap cq_ring");

	sqes = mmap(NULL, params.sq_entries * sizeof(*sqes),
		    PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd, 0x20000000);
	if (sqes == MAP_FAILED)
		fatal("mmap sqes");

	cqes = mmap(NULL, params.cq_entries * sizeof(*cqes),
		    PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd, 0x30000000);
	if (cqes == MAP_FAILED)
		fatal("mmap cqes");

	sq_head = (uint32_t *)sq_ring;
	sq_tail = sq_head + 1;
	sq_mask = sq_head + 2;

	cq_head = (uint32_t *)cq_ring;
	cq_tail = cq_head + 1;
	cq_mask = cq_head + 2;

	fd = open("/tmp/xsc-demo.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0)
		fatal("open demo file");

	write(fd, "ring demo", 9);
	lseek(fd, 0, SEEK_SET);

	sqe = &sqes[*sq_tail & *sq_mask];
	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = XSC_OP_READ;
	sqe->fd = fd;
	sqe->addr = (uintptr_t)buf;
	sqe->len = sizeof(buf);
	sqe->user_data = 0xdeadbeef;

	__sync_synchronize();
	*sq_tail += 1;

	struct pollfd pfd = {
		.fd = xsc_fd,
		.events = POLLIN
	};

	if (poll(&pfd, 1, 1000) <= 0)
		fatal("poll");

	cqe = &cqes[*cq_head & *cq_mask];
	printf("CQE: user_data=0x%llx res=%d\n",
	       (unsigned long long)cqe->user_data, cqe->res);
	if (cqe->res > 0)
		printf("Data: %.*s\n", cqe->res, buf);

	*cq_head += 1;

	return 0;
}
