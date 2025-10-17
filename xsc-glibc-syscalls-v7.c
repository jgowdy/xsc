/*
 * XSC v7 Ring-Based Syscall Implementation for glibc
 *
 * This implements the complete syscall() function replacement that routes
 * all system calls through XSC shared-memory rings instead of trap instructions.
 *
 * Based on XSC OS Design v7 specification.
 */

#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <stdatomic.h>

/* XSC Ring Structures - must match kernel xsc_uapi.h */

struct xsc_sqe {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t ioprio;
    int32_t  fd;
    uint64_t addr;
    uint32_t len;
    uint64_t user_data;
    uint64_t offset;
    union {
        uint32_t open_flags;
        uint32_t fsync_flags;
        uint32_t futex_val;
        uint32_t timeout_flags;
    };
    uint64_t clone_flags;
    uint64_t addr2;
    uint32_t reserved[4];
} __attribute__((packed));

struct xsc_cqe {
    uint64_t user_data;
    int32_t  res;
    uint32_t flags;
} __attribute__((packed));

/* Ring metadata structures */
struct xsc_sqe_ring {
    uint32_t head;
    uint32_t tail;
    uint32_t mask;
    uint32_t flags;
};

struct xsc_cqe_ring {
    uint32_t head;
    uint32_t tail;
    uint32_t mask;
    uint32_t overflow;
};

/* XSC Opcodes - must match kernel */
#define XSC_OP_READ         1
#define XSC_OP_WRITE        2
#define XSC_OP_OPEN         3
#define XSC_OP_CLOSE        4
#define XSC_OP_STAT         5
#define XSC_OP_FSTAT        6
#define XSC_OP_LSTAT        7
#define XSC_OP_POLL         8
#define XSC_OP_LSEEK        9
#define XSC_OP_MMAP         10
#define XSC_OP_MPROTECT     11
#define XSC_OP_MUNMAP       12
#define XSC_OP_BRK          13
#define XSC_OP_SIGACTION    14
#define XSC_OP_SIGPROCMASK  15
#define XSC_OP_IOCTL        16
#define XSC_OP_READV        17
#define XSC_OP_WRITEV       18
#define XSC_OP_PREAD        19
#define XSC_OP_PWRITE       20
#define XSC_OP_SOCKET       30
#define XSC_OP_CONNECT      31
#define XSC_OP_ACCEPT       32
#define XSC_OP_SENDTO       33
#define XSC_OP_RECVFROM     34
#define XSC_OP_BIND         35
#define XSC_OP_LISTEN       36
#define XSC_OP_NANOSLEEP    40
#define XSC_OP_CLOCK_NANOSLEEP 41
#define XSC_OP_SELECT       42
#define XSC_OP_EPOLL_WAIT   43
#define XSC_OP_FUTEX_WAIT   50
#define XSC_OP_FUTEX_WAKE   51
#define XSC_OP_FORK         60
#define XSC_OP_VFORK        61
#define XSC_OP_CLONE        62
#define XSC_OP_EXECVE       63
#define XSC_OP_EXECVEAT     64
#define XSC_OP_FSYNC        70

/* XSC ioctl commands */
#define XSC_IOC_SETUP _IOW('X', 1, struct xsc_params)

struct xsc_params {
    uint32_t sq_entries;
    uint32_t cq_entries;
};

/* Global XSC state */
static int xsc_fd = -1;
static struct xsc_sqe_ring *sq_ring = NULL;
static struct xsc_cqe_ring *cq_ring = NULL;
static struct xsc_sqe *sqes = NULL;
static struct xsc_cqe *cqes = NULL;
static uint32_t sq_size = 128;
static uint32_t cq_size = 256;
static _Atomic uint64_t next_user_data = 1;

/*
 * Initialize XSC rings from /dev/xsc
 *
 * v7 spec calls for auxv-based initialization, but for minimal ISO
 * we use /dev/xsc opening. Auxv support will be added in kernel later.
 */
static int __xsc_init_dev(void) {
    struct xsc_params params;

    /* Open XSC device */
    xsc_fd = open("/dev/xsc", O_RDWR | O_CLOEXEC);
    if (xsc_fd < 0) {
        return -1;
    }

    /* Setup rings */
    params.sq_entries = sq_size;
    params.cq_entries = cq_size;

    if (ioctl(xsc_fd, XSC_IOC_SETUP, &params) < 0) {
        close(xsc_fd);
        xsc_fd = -1;
        return -1;
    }

    /* Map SQ ring metadata (offset 0x00000000) */
    sq_ring = mmap(NULL, sizeof(struct xsc_sqe_ring),
                   PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd, 0);
    if (sq_ring == MAP_FAILED) {
        goto err_close;
    }

    /* Map CQ ring metadata (offset 0x10000000) */
    cq_ring = mmap(NULL, sizeof(struct xsc_cqe_ring),
                   PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd, 0x10000000);
    if (cq_ring == MAP_FAILED) {
        goto err_unmap_sq;
    }

    /* Map SQE array (offset 0x20000000) */
    sqes = mmap(NULL, sq_size * sizeof(struct xsc_sqe),
                PROT_READ | PROT_WRITE, MAP_SHARED, xsc_fd, 0x20000000);
    if (sqes == MAP_FAILED) {
        goto err_unmap_cq;
    }

    /* Map CQE array (offset 0x30000000) */
    cqes = mmap(NULL, cq_size * sizeof(struct xsc_cqe),
                PROT_READ, MAP_SHARED, xsc_fd, 0x30000000);
    if (cqes == MAP_FAILED) {
        goto err_unmap_sqes;
    }

    return 0;

err_unmap_sqes:
    munmap(sqes, sq_size * sizeof(struct xsc_sqe));
err_unmap_cq:
    munmap(cq_ring, sizeof(struct xsc_cqe_ring));
err_unmap_sq:
    munmap(sq_ring, sizeof(struct xsc_sqe_ring));
err_close:
    close(xsc_fd);
    xsc_fd = -1;
    return -1;
}

/*
 * Initialize XSC from auxv (future v7 full implementation)
 * For now, falls back to /dev/xsc
 */
int __xsc_init(void) {
    if (xsc_fd >= 0) {
        return 0;  /* Already initialized */
    }

    /* TODO: Read from auxv AT_XSC_* entries when kernel provides them */
    /* For now, use /dev/xsc */
    return __xsc_init_dev();
}

/*
 * Notify kernel that new SQEs are available
 * Triggers xsc_write() in kernel which queues worker
 */
static inline void __xsc_notify_kernel(void) {
    /* Write any byte to /dev/xsc to wake workers */
    char dummy = 1;
    write(xsc_fd, &dummy, 1);
}

/*
 * Submit SQE and wait for completion
 * This is the synchronous syscall path used by most glibc functions
 */
static long __xsc_submit_sync(struct xsc_sqe *sqe) {
    uint32_t tail, mask;
    uint64_t my_user_data;
    struct xsc_sqe *sq_entry;
    struct pollfd pfd;

    /* Lazy init */
    if (xsc_fd < 0) {
        if (__xsc_init() < 0) {
            errno = ENOSYS;
            return -1;
        }
    }

    /* Assign unique user_data for tracking */
    my_user_data = atomic_fetch_add(&next_user_data, 1);
    sqe->user_data = my_user_data;

    /* Get next SQ slot with atomic load */
    tail = atomic_load_explicit((_Atomic uint32_t *)&sq_ring->tail,
                                  memory_order_acquire);
    mask = atomic_load_explicit((_Atomic uint32_t *)&sq_ring->mask,
                                 memory_order_relaxed);

    /* Write SQE to ring */
    sq_entry = &sqes[tail & mask];
    *sq_entry = *sqe;

    /* Memory barrier: ensure SQE is visible before tail update */
    atomic_thread_fence(memory_order_release);

    /* Update tail */
    atomic_store_explicit((_Atomic uint32_t *)&sq_ring->tail, tail + 1,
                          memory_order_release);

    /* Notify kernel workers */
    __xsc_notify_kernel();

    /* Wait for completion */
    pfd.fd = xsc_fd;
    pfd.events = POLLIN;

    while (1) {
        uint32_t cq_head, cq_tail, cq_mask;

        /* Load CQ pointers with acquire semantics */
        cq_head = atomic_load_explicit((_Atomic uint32_t *)&cq_ring->head,
                                        memory_order_acquire);
        cq_tail = atomic_load_explicit((_Atomic uint32_t *)&cq_ring->tail,
                                        memory_order_acquire);
        cq_mask = atomic_load_explicit((_Atomic uint32_t *)&cq_ring->mask,
                                        memory_order_relaxed);

        /* Scan CQ for our completion */
        while (cq_head != cq_tail) {
            struct xsc_cqe *cqe = &cqes[cq_head & cq_mask];

            if (cqe->user_data == my_user_data) {
                long result = cqe->res;

                /* Advance head */
                atomic_store_explicit((_Atomic uint32_t *)&cq_ring->head,
                                      cq_head + 1, memory_order_release);

                /* Set errno if syscall failed */
                if (result < 0) {
                    errno = -result;
                    return -1;
                }
                return result;
            }
            cq_head++;
        }

        /* No completion yet, wait for kernel to signal */
        poll(&pfd, 1, -1);
    }
}

/*
 * Syscall wrappers using XSC rings
 * These replace the standard syscall implementations in glibc
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

pid_t __xsc_fork(void) {
    struct xsc_sqe sqe = {0};
    sqe.opcode = XSC_OP_FORK;
    return __xsc_submit_sync(&sqe);
}

int __xsc_execve(const char *pathname, char *const argv[], char *const envp[]) {
    struct xsc_sqe sqe = {0};
    sqe.opcode = XSC_OP_EXECVE;
    sqe.addr = (uint64_t)pathname;
    sqe.addr2 = (uint64_t)argv;
    sqe.offset = (uint64_t)envp;
    return __xsc_submit_sync(&sqe);
}

/* Export symbols for glibc linking */
extern ssize_t __libc_read(int fd, void *buf, size_t count)
    __attribute__((alias("__xsc_read")));

extern ssize_t __libc_write(int fd, const void *buf, size_t count)
    __attribute__((alias("__xsc_write")));

extern int __libc_open(const char *pathname, int flags, ...)
    __attribute__((alias("__xsc_open")));

extern int __libc_close(int fd)
    __attribute__((alias("__xsc_close")));

extern pid_t __libc_fork(void)
    __attribute__((alias("__xsc_fork")));

extern int __libc_execve(const char *pathname, char *const argv[], char *const envp[])
    __attribute__((alias("__xsc_execve")));
