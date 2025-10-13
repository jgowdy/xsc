/*
 * XSC Ring-Based Syscall Implementation
 * For glibc sysdeps/unix/sysv/linux/x86_64-xsc/
 *
 * Based on FlexSC research by Livio Soares and Michael Stumm
 * (OSDI 2010, University of Toronto)
 * https://www.usenix.org/legacy/event/osdi10/tech/full_papers/Soares.pdf
 *
 * XSC extends FlexSC with:
 * - Production implementation for modern Linux
 * - Hardware CFI enforcement (Intel CET, ARM PAC)
 * - Complete distribution infrastructure
 *
 * "All I want to do is... an take your syscall"
 */

#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

/* XSC Ring Structures */
struct xsc_sqe {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t ioprio;
    int32_t  fd;
    uint64_t addr;
    uint32_t len;
    uint64_t user_data;
    uint64_t offset;
    uint32_t open_flags;
    uint32_t fsync_flags;
    uint32_t futex_val;
    uint32_t timeout_flags;
    uint64_t clone_flags;
    uint64_t addr2;
    uint32_t reserved[4];
} __attribute__((packed));

struct xsc_cqe {
    uint64_t user_data;
    int32_t  res;
    uint32_t flags;
} __attribute__((packed));

/* XSC Opcodes */
#define XSC_OP_READ     1
#define XSC_OP_WRITE    2
#define XSC_OP_OPEN     3
#define XSC_OP_CLOSE    4
#define XSC_OP_STAT     5
#define XSC_OP_LSEEK    6
#define XSC_OP_FSYNC    7
#define XSC_OP_FORK     10
#define XSC_OP_EXECVE   11
#define XSC_OP_EXIT     12
#define XSC_OP_WAIT     13
#define XSC_OP_CLONE    14
#define XSC_OP_MMAP     20
#define XSC_OP_MUNMAP   21
#define XSC_OP_MPROTECT 22
#define XSC_OP_BRK      23
#define XSC_OP_SOCKET   30
#define XSC_OP_BIND     31
#define XSC_OP_LISTEN   32
#define XSC_OP_ACCEPT   33
#define XSC_OP_CONNECT  34
#define XSC_OP_SEND     35
#define XSC_OP_RECV     36
#define XSC_OP_PIPE     40
#define XSC_OP_FUTEX    41

/* Global XSC state */
static int xsc_fd = -1;
static struct xsc_sqe *sq_ring = NULL;
static struct xsc_cqe *cq_ring = NULL;
static uint32_t sq_size = 128;
static uint32_t cq_size = 128;
static uint32_t sq_head = 0;
static uint32_t sq_tail = 0;
static uint32_t cq_head = 0;

/* XSC ioctl commands */
#define XSC_SETUP_RINGS _IOW('X', 1, struct xsc_setup)

struct xsc_setup {
    uint32_t sq_entries;
    uint32_t cq_entries;
};

/*
 * Initialize XSC device and rings
 * Called once at program startup
 */
int __xsc_init(void) {
    if (xsc_fd >= 0) return 0; /* Already initialized */

    /* Open XSC device - uses standard syscall */
    xsc_fd = open("/dev/xsc", O_RDWR);
    if (xsc_fd < 0) {
        return -1;
    }

    /* Setup rings */
    struct xsc_setup setup = {
        .sq_entries = sq_size,
        .cq_entries = cq_size
    };

    if (ioctl(xsc_fd, XSC_SETUP_RINGS, &setup) < 0) {
        close(xsc_fd);
        xsc_fd = -1;
        return -1;
    }

    /* Map submission queue */
    sq_ring = mmap(NULL, sq_size * sizeof(struct xsc_sqe),
                   PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd, 0);
    if (sq_ring == MAP_FAILED) {
        close(xsc_fd);
        xsc_fd = -1;
        return -1;
    }

    /* Map completion queue */
    cq_ring = mmap(NULL, cq_size * sizeof(struct xsc_cqe),
                   PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd,
                   sq_size * sizeof(struct xsc_sqe));
    if (cq_ring == MAP_FAILED) {
        munmap(sq_ring, sq_size * sizeof(struct xsc_sqe));
        close(xsc_fd);
        xsc_fd = -1;
        return -1;
    }

    return 0;
}

/*
 * Submit SQE and wait for completion
 * This is the synchronous syscall path
 */
static long __xsc_submit_sync(struct xsc_sqe *sqe) {
    /* Lazy init */
    if (xsc_fd < 0) {
        if (__xsc_init() < 0) {
            errno = ENOSYS;
            return -1;
        }
    }

    /* Get next SQ slot */
    uint32_t tail = sq_tail;
    struct xsc_sqe *sq_entry = &sq_ring[tail % sq_size];

    /* Copy SQE */
    *sq_entry = *sqe;
    sq_entry->user_data = (uint64_t)tail;

    /* Advance tail */
    sq_tail = tail + 1;

    /* Notify kernel - write tail pointer */
    /* In real impl, would use shared memory location or ioctl */

    /* Wait for completion */
    while (1) {
        uint32_t head = cq_head;
        if (head < sq_tail) {
            struct xsc_cqe *cqe = &cq_ring[head % cq_size];
            if (cqe->user_data == (uint64_t)tail) {
                long result = cqe->res;
                cq_head = head + 1;
                return result;
            }
        }
        /* In real impl, would sleep or use futex here */
    }
}

/*
 * Syscall wrappers using XSC rings
 */

ssize_t __xsc_read(int fd, void *buf, size_t count) {
    struct xsc_sqe sqe = {0};
    sqe.opcode = XSC_OP_READ;
    sqe.fd = fd;
    sqe.addr = (uint64_t)buf;
    sqe.len = count;
    return __xsc_submit_sync(&sqe);
}

ssize_t __xsc_write(int fd, const void *buf, size_t count) {
    struct xsc_sqe sqe = {0};
    sqe.opcode = XSC_OP_WRITE;
    sqe.fd = fd;
    sqe.addr = (uint64_t)buf;
    sqe.len = count;
    return __xsc_submit_sync(&sqe);
}

int __xsc_open(const char *pathname, int flags, mode_t mode) {
    struct xsc_sqe sqe = {0};
    sqe.opcode = XSC_OP_OPEN;
    sqe.addr = (uint64_t)pathname;
    sqe.open_flags = flags;
    sqe.len = mode;
    return __xsc_submit_sync(&sqe);
}

int __xsc_close(int fd) {
    struct xsc_sqe sqe = {0};
    sqe.opcode = XSC_OP_CLOSE;
    sqe.fd = fd;
    return __xsc_submit_sync(&sqe);
}

/* Export symbols for glibc */
#define __libc_read __xsc_read
#define __libc_write __xsc_write
#define __libc_open __xsc_open
#define __libc_close __xsc_close
