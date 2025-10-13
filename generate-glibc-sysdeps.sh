#!/bin/bash
# Generate complete glibc sysdeps/unix/sysv/linux/xsc/ implementation

SYSDEPS_DIR="sysdeps/unix/sysv/linux/xsc"

# Create all sysdeps/xsc files on the server
cat > /tmp/create_glibc_xsc.sh <<'EOFSCRIPT'
#!/bin/bash
set -e

SYSDEPS="$HOME/xsc-build/glibc/glibc-2.36/sysdeps/unix/sysv/linux/xsc"
mkdir -p "$SYSDEPS"

# 1. fork.c
cat > "$SYSDEPS/fork.c" <<'EOF'
/* XSC fork implementation */
#include <unistd.h>
#include <stdint.h>

extern int __xsc_submit_fork(void);

pid_t
__libc_fork (void)
{
#ifdef __GLIBC_XSC__
  return __xsc_submit_fork();
#else
  return -1;
#endif
}
weak_alias (__libc_fork, fork)
libc_hidden_def (__libc_fork)
EOF

# 2. vfork.c
cat > "$SYSDEPS/vfork.c" <<'EOF'
/* XSC vfork implementation */
#include <unistd.h>

extern int __xsc_submit_vfork(void);

pid_t
__vfork (void)
{
#ifdef __GLIBC_XSC__
  return __xsc_submit_vfork();
#else
  return -1;
#endif
}
weak_alias (__vfork, vfork)
libc_hidden_def (__vfork)
EOF

# 3. clone-internal.c
cat > "$SYSDEPS/clone-internal.c" <<'EOF'
/* XSC clone implementation */
#include <sched.h>
#include <stdint.h>

extern int __xsc_submit_clone(unsigned long flags, void *stack);

int
__clone_internal (struct clone_args *cl_args,
                  int (*fn) (void *), void *arg)
{
#ifdef __GLIBC_XSC__
  return __xsc_submit_clone(cl_args->flags, cl_args->stack);
#else
  return -1;
#endif
}
libc_hidden_def (__clone_internal)
EOF

# 4. execve.c
cat > "$SYSDEPS/execve.c" <<'EOF'
/* XSC execve implementation */
#include <unistd.h>

extern int __xsc_submit_execve(const char *path, char *const argv[],
                                 char *const envp[]);

int
__execve (const char *path, char *const argv[], char *const envp[])
{
#ifdef __GLIBC_XSC__
  return __xsc_submit_execve(path, argv, envp);
#else
  return -1;
#endif
}
weak_alias (__execve, execve)
libc_hidden_def (__execve)
EOF

# 5. execveat.c
cat > "$SYSDEPS/execveat.c" <<'EOF'
/* XSC execveat implementation */
#include <unistd.h>
#include <fcntl.h>

extern int __xsc_submit_execveat(int dirfd, const char *path,
                                   char *const argv[], char *const envp[],
                                   int flags);

int
__execveat (int dirfd, const char *path, char *const argv[],
            char *const envp[], int flags)
{
#ifdef __GLIBC_XSC__
  return __xsc_submit_execveat(dirfd, path, argv, envp, flags);
#else
  return -1;
#endif
}
weak_alias (__execveat, execveat)
EOF

# 6. clock_gettime.c
cat > "$SYSDEPS/clock_gettime.c" <<'EOF'
/* XSC clock_gettime - vDSO only */
#include <time.h>
#include <sys/time.h>

extern int __vdso_clock_gettime (clockid_t, struct timespec *);

int
__clock_gettime (clockid_t clock_id, struct timespec *tp)
{
  return __vdso_clock_gettime (clock_id, tp);
}
weak_alias (__clock_gettime, clock_gettime)
libc_hidden_def (__clock_gettime)
EOF

# 7. gettimeofday.c
cat > "$SYSDEPS/gettimeofday.c" <<'EOF'
/* XSC gettimeofday - vDSO only */
#include <sys/time.h>

extern int __vdso_gettimeofday (struct timeval *, void *);

int
__gettimeofday (struct timeval *tv, struct timezone *tz)
{
  return __vdso_gettimeofday (tv, tz);
}
weak_alias (__gettimeofday, gettimeofday)
libc_hidden_def (__gettimeofday)
EOF

# 8. getcpu.c
cat > "$SYSDEPS/getcpu.c" <<'EOF'
/* XSC getcpu - vDSO only */
#include <sched.h>

extern int __vdso_getcpu (unsigned *, unsigned *);

int
__getcpu (unsigned *cpu, unsigned *node)
{
  return __vdso_getcpu (cpu, node);
}
weak_alias (__getcpu, getcpu)
EOF

# 9. getpid.c
cat > "$SYSDEPS/getpid.c" <<'EOF'
/* XSC getpid - vDSO only */
#include <unistd.h>

extern pid_t __vdso_getpid (void);

pid_t
__getpid (void)
{
  return __vdso_getpid ();
}
weak_alias (__getpid, getpid)
libc_hidden_def (__getpid)
EOF

# 10. gettid.c
cat > "$SYSDEPS/gettid.c" <<'EOF'
/* XSC gettid - vDSO only */
#include <unistd.h>
#include <sys/types.h>

extern pid_t __vdso_gettid (void);

pid_t
__gettid (void)
{
  return __vdso_gettid ();
}
weak_alias (__gettid, gettid)
EOF

# 11. futex-internal.c
cat > "$SYSDEPS/futex-internal.c" <<'EOF'
/* XSC futex implementation */
#include <sysdep.h>
#include <futex-internal.h>

extern int __xsc_futex_wait(uint32_t *uaddr, uint32_t val,
                             const struct timespec *timeout);
extern int __xsc_futex_wake(uint32_t *uaddr, int nr);

int
__futex_abstimed_wait_common (unsigned int *futex_word,
                               unsigned int expected,
                               clockid_t clockid,
                               const struct __timespec64 *abstime,
                               int private, bool cancel)
{
#ifdef __GLIBC_XSC__
  return __xsc_futex_wait(futex_word, expected,
                          (const struct timespec *)abstime);
#else
  return -1;
#endif
}

int
__futex_wake (unsigned int *futex_word, int processes_to_wake, int private)
{
#ifdef __GLIBC_XSC__
  return __xsc_futex_wake(futex_word, processes_to_wake);
#else
  return -1;
#endif
}
EOF

# 12. poll.c
cat > "$SYSDEPS/poll.c" <<'EOF'
/* XSC poll implementation */
#include <sys/poll.h>

extern int __xsc_poll(struct pollfd *fds, nfds_t nfds, int timeout);

int
__poll (struct pollfd *fds, nfds_t nfds, int timeout)
{
#ifdef __GLIBC_XSC__
  return __xsc_poll(fds, nfds, timeout);
#else
  return -1;
#endif
}
weak_alias (__poll, poll)
libc_hidden_def (__poll)
EOF

# 13. epoll_wait.c
cat > "$SYSDEPS/epoll_wait.c" <<'EOF'
/* XSC epoll_wait implementation */
#include <sys/epoll.h>

extern int __xsc_epoll_wait(int epfd, struct epoll_event *events,
                             int maxevents, int timeout);

int
__epoll_wait (int epfd, struct epoll_event *events,
              int maxevents, int timeout)
{
#ifdef __GLIBC_XSC__
  return __xsc_epoll_wait(epfd, events, maxevents, timeout);
#else
  return -1;
#endif
}
weak_alias (__epoll_wait, epoll_wait)
libc_hidden_def (__epoll_wait)
EOF

# 14. select.c
cat > "$SYSDEPS/select.c" <<'EOF'
/* XSC select implementation */
#include <sys/select.h>

extern int __xsc_select(int nfds, fd_set *readfds, fd_set *writefds,
                        fd_set *exceptfds, struct timeval *timeout);

int
__select (int nfds, fd_set *readfds, fd_set *writefds,
          fd_set *exceptfds, struct timeval *timeout)
{
#ifdef __GLIBC_XSC__
  return __xsc_select(nfds, readfds, writefds, exceptfds, timeout);
#else
  return -1;
#endif
}
weak_alias (__select, select)
libc_hidden_def (__select)
EOF

# 15. Makefile for sysdeps/xsc
cat > "$SYSDEPS/Makefile" <<'EOF'
# XSC-specific makefile rules
ifeq ($(subdir),misc)
sysdep_routines += xsc-init
endif

ifeq ($(subdir),posix)
sysdep_routines += fork vfork clone-internal execve execveat
sysdep_routines += getpid gettid
endif

ifeq ($(subdir),rt)
sysdep_routines += clock_gettime
endif

ifeq ($(subdir),time)
sysdep_routines += gettimeofday
endif

ifeq ($(subdir),nptl)
sysdep_routines += futex-internal
endif

ifeq ($(subdir),io)
sysdep_routines += poll select epoll_wait getcpu
endif
EOF

echo "Created all glibc sysdeps/xsc files in $SYSDEPS"
ls -la "$SYSDEPS"
EOFSCRIPT

# Execute on server
scp /tmp/create_glibc_xsc.sh bx.ee:/tmp/
ssh bx.ee "chmod +x /tmp/create_glibc_xsc.sh && /tmp/create_glibc_xsc.sh"
