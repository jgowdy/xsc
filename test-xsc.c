/*
 * XSC Test Program - Demonstrates ring-based syscall interface
 *
 * This program tests the XSC (eXtended Syscall) mechanism by:
 * 1. Opening /dev/xsc
 * 2. Setting up submission and completion queue rings
 * 3. Submitting a simple READ operation
 * 4. Polling for completion
 * 5. Displaying the result
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

/* XSC UAPI definitions */
#define XSC_OP_READ    1
#define XSC_OP_WRITE   2
#define XSC_OP_CLOSE   4

struct xsc_sqe {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	ioprio;
	int32_t		fd;
	union {
		uint64_t	off;
		uint64_t	addr2;
	};
	union {
		uint64_t	addr;
		uint64_t	splice_off_in;
	};
	uint32_t	len;
	uint32_t	rw_flags;
	uint64_t	user_data;
	uint16_t	buf_index;
	uint16_t	personality;
	uint32_t	file_index;
	uint64_t	__pad2[2];
};

struct xsc_cqe {
	uint64_t	user_data;
	int32_t		res;
	uint32_t	flags;
};

struct xsc_sqe_ring {
	uint32_t	head;
	uint32_t	tail;
	uint32_t	ring_mask;
	uint32_t	ring_entries;
	uint32_t	flags;
	uint32_t	dropped;
	uint32_t	array;
	uint32_t	resv1;
	uint64_t	resv2;
};

struct xsc_cqe_ring {
	uint32_t	head;
	uint32_t	tail;
	uint32_t	ring_mask;
	uint32_t	ring_entries;
	uint32_t	overflow;
	uint32_t	cqes;
	uint64_t	resv[2];
};

struct xsc_params {
	uint32_t	sq_entries;
	uint32_t	cq_entries;
	uint32_t	flags;
	uint32_t	sq_thread_cpu;
	uint32_t	sq_thread_idle;
	uint32_t	features;
	uint32_t	wq_fd;
	uint32_t	resv[3];
	struct xsc_sqe_ring sq_off;
	struct xsc_cqe_ring cq_off;
};

#define XSC_IOC_MAGIC		'x'
#define XSC_IOC_SETUP		_IOWR(XSC_IOC_MAGIC, 0, struct xsc_params)

/* Mmap offsets */
#define XSC_OFF_SQ_RING		0x00000000ULL
#define XSC_OFF_CQ_RING		0x10000000ULL
#define XSC_OFF_SQES		0x20000000ULL
#define XSC_OFF_CQES		0x30000000ULL

struct xsc_ring_ctx {
	int fd;

	/* Ring metadata */
	struct xsc_sqe_ring *sq_ring;
	struct xsc_cqe_ring *cq_ring;

	/* Entry arrays */
	struct xsc_sqe *sqes;
	struct xsc_cqe *cqes;

	uint32_t sq_entries;
	uint32_t cq_entries;
};

static int xsc_setup(struct xsc_ring_ctx *ctx, uint32_t sq_entries, uint32_t cq_entries)
{
	struct xsc_params params;
	size_t sq_ring_size, cq_ring_size, sqe_size, cqe_size;
	int ret;

	memset(&params, 0, sizeof(params));
	params.sq_entries = sq_entries;
	params.cq_entries = cq_entries;

	printf("Calling ioctl: fd=%d, cmd=0x%lx, params addr=%p\n",
	       ctx->fd, (unsigned long)XSC_IOC_SETUP, (void*)&params);
	printf("params.sq_entries=%u, params.cq_entries=%u\n",
	       params.sq_entries, params.cq_entries);

	ret = ioctl(ctx->fd, XSC_IOC_SETUP, &params);
	if (ret < 0) {
		printf("ioctl returned: %d, errno=%d (%s)\n", ret, errno, strerror(errno));
		perror("ioctl XSC_IOC_SETUP");
		return -1;
	}

	ctx->sq_entries = params.sq_entries;
	ctx->cq_entries = params.cq_entries;

	printf("Ring setup successful: SQ=%u entries, CQ=%u entries\n",
	       ctx->sq_entries, ctx->cq_entries);

	/* Mmap SQ ring metadata */
	sq_ring_size = sizeof(struct xsc_sqe_ring);
	ctx->sq_ring = mmap(NULL, sq_ring_size, PROT_READ | PROT_WRITE,
			    MAP_SHARED | MAP_POPULATE, ctx->fd, XSC_OFF_SQ_RING);
	if (ctx->sq_ring == MAP_FAILED) {
		perror("mmap SQ ring");
		return -1;
	}

	/* Mmap CQ ring metadata */
	cq_ring_size = sizeof(struct xsc_cqe_ring);
	ctx->cq_ring = mmap(NULL, cq_ring_size, PROT_READ | PROT_WRITE,
			    MAP_SHARED | MAP_POPULATE, ctx->fd, XSC_OFF_CQ_RING);
	if (ctx->cq_ring == MAP_FAILED) {
		perror("mmap CQ ring");
		return -1;
	}

	/* Mmap SQE array */
	sqe_size = ctx->sq_entries * sizeof(struct xsc_sqe);
	ctx->sqes = mmap(NULL, sqe_size, PROT_READ | PROT_WRITE,
			 MAP_SHARED | MAP_POPULATE, ctx->fd, XSC_OFF_SQES);
	if (ctx->sqes == MAP_FAILED) {
		perror("mmap SQEs");
		return -1;
	}

	/* Mmap CQE array */
	cqe_size = ctx->cq_entries * sizeof(struct xsc_cqe);
	ctx->cqes = mmap(NULL, cqe_size, PROT_READ | PROT_WRITE,
			 MAP_SHARED | MAP_POPULATE, ctx->fd, XSC_OFF_CQES);
	if (ctx->cqes == MAP_FAILED) {
		perror("mmap CQEs");
		return -1;
	}

	printf("Ring buffers mapped successfully\n");
	return 0;
}

static void xsc_submit_sqe(struct xsc_ring_ctx *ctx, struct xsc_sqe *sqe)
{
	uint32_t tail, mask, index;

	tail = ctx->sq_ring->tail;
	mask = ctx->sq_ring->ring_mask;
	index = tail & mask;

	/* Copy SQE to the ring */
	memcpy(&ctx->sqes[index], sqe, sizeof(*sqe));

	/* Update tail */
	ctx->sq_ring->tail = tail + 1;

	printf("Submitted SQE: opcode=%u, fd=%d, user_data=%lu\n",
	       sqe->opcode, sqe->fd, sqe->user_data);
}

static int xsc_wait_cqe(struct xsc_ring_ctx *ctx, struct xsc_cqe *cqe_out, int timeout_ms)
{
	uint32_t head, tail, mask, index;
	int i;

	/* Simple polling loop */
	for (i = 0; i < timeout_ms; i++) {
		head = ctx->cq_ring->head;
		tail = ctx->cq_ring->tail;

		if (head != tail) {
			/* We have a completion */
			mask = ctx->cq_ring->ring_mask;
			index = head & mask;

			memcpy(cqe_out, &ctx->cqes[index], sizeof(*cqe_out));

			/* Update head */
			ctx->cq_ring->head = head + 1;

			printf("Received CQE: user_data=%lu, res=%d\n",
			       cqe_out->user_data, cqe_out->res);
			return 0;
		}

		usleep(1000); /* Sleep 1ms */
	}

	return -1; /* Timeout */
}

int main(int argc, char **argv)
{
	struct xsc_ring_ctx ctx;
	struct xsc_sqe sqe;
	struct xsc_cqe cqe;
	char buffer[256];
	int test_fd;
	int ret;

	printf("XSC Test Program\n");
	printf("================\n\n");

	/* Open XSC device */
	ctx.fd = open("/dev/xsc", O_RDWR);
	if (ctx.fd < 0) {
		perror("open /dev/xsc");
		printf("\nNote: Make sure the XSC kernel module is loaded (modprobe xsc)\n");
		return 1;
	}
	printf("Opened /dev/xsc successfully (fd=%d)\n\n", ctx.fd);

	/* Setup rings */
	printf("About to call xsc_setup with sq_entries=128, cq_entries=256\n");
	printf("XSC_IOC_SETUP value: 0x%lx\n", (unsigned long)XSC_IOC_SETUP);
	printf("sizeof(struct xsc_params): %zu\n", sizeof(struct xsc_params));
	if (xsc_setup(&ctx, 128, 256) < 0) {
		close(ctx.fd);
		return 1;
	}
	printf("\n");

	/* Create a test file for reading */
	test_fd = open("/tmp/xsc-test.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (test_fd < 0) {
		perror("open test file");
		close(ctx.fd);
		return 1;
	}

	const char *test_data = "Hello from XSC ring-based syscalls!\n";
	write(test_fd, test_data, strlen(test_data));
	lseek(test_fd, 0, SEEK_SET);
	printf("Created test file /tmp/xsc-test.txt\n\n");

	/* Submit a READ operation via XSC */
	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = XSC_OP_READ;
	sqe.fd = test_fd;
	sqe.addr = (uint64_t)buffer;
	sqe.len = sizeof(buffer) - 1;
	sqe.user_data = 0x12345678;  /* Arbitrary user data */

	printf("Submitting READ operation via XSC...\n");
	xsc_submit_sqe(&ctx, &sqe);

	/* Trigger submission queue processing */
	write(ctx.fd, "", 1);
	printf("\n");

	/* Wait for completion */
	printf("Waiting for completion...\n");
	ret = xsc_wait_cqe(&ctx, &cqe, 5000);  /* 5 second timeout */
	if (ret < 0) {
		printf("ERROR: Timeout waiting for completion\n");
		close(test_fd);
		close(ctx.fd);
		return 1;
	}
	printf("\n");

	/* Check result */
	if (cqe.res > 0) {
		buffer[cqe.res] = '\0';
		printf("SUCCESS! Read %d bytes via XSC:\n", cqe.res);
		printf("Data: %s\n", buffer);
	} else {
		printf("READ failed with error: %d (%s)\n", cqe.res, strerror(-cqe.res));
	}

	/* Cleanup */
	close(test_fd);
	unlink("/tmp/xsc-test.txt");
	close(ctx.fd);

	printf("\nXSC test complete!\n");
	return 0;
}
