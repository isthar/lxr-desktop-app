/*
 * plug_rtcheck.c
 *
 * libplug_rtcheck.dylib, the real-time check of the plugin's audio thread (plan E9.2 Task 9):
 * loaded into lichomat-plughost with DYLD_INSERT_LIBRARIES, it interposes the calls an audio
 * thread must not make (allocation, locks, waits, sleeps, file, pipe and memory system calls,
 * processes, threads and dispatch) and counts those made while the host has marked its thread as
 * inside process() (plug_rtInside()). write() is counted apart: the plugin makes one,
 * non-blocking, to wake its engine. Each forbidden function that was called is printed at exit.
 * The list is of calls, not of every system call: one that is not in it is not seen.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <os/lock.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* the forbidden calls, in the order of their counters */
#define RT_CALLS(X) \
	X(malloc) X(calloc) X(realloc) X(free) X(posix_memalign) \
	X(pthread_mutex_lock) X(pthread_cond_wait) X(pthread_cond_timedwait) X(pthread_cond_signal) \
	X(pthread_cond_broadcast) X(pthread_rwlock_rdlock) X(pthread_rwlock_wrlock) X(os_unfair_lock_lock) X(sem_wait) \
	X(dispatch_semaphore_wait) X(dispatch_sync) X(dispatch_async) X(pthread_create) X(pthread_join) \
	X(nanosleep) X(usleep) X(sleep) X(sched_yield) \
	X(read) X(poll) X(select) X(open) X(openat) X(close) X(fcntl) X(ioctl) X(lseek) X(stat) X(fstat) \
	X(mmap) X(munmap) X(mprotect) \
	X(getpid) X(getppid) X(kill) X(fork) X(posix_spawn) X(posix_spawnp) X(waitpid)
#define RT_ENUM(f) RT_##f,
#define RT_NAME(f) #f,
enum { RT_CALLS(RT_ENUM) RT_N };
static const char *const names[RT_N] = { RT_CALLS(RT_NAME) };

/* the marked thread; no thread-local variable: their first use allocates, which recurses here */
static pthread_t marked;
static int inside;
static unsigned long long violations, writes;
static unsigned long long counts[RT_N];

static int marking(void)
{
	return __atomic_load_n(&inside, __ATOMIC_ACQUIRE) && pthread_equal(pthread_self(), marked);
}

static void hit(int k)
{
	if (marking())
	{
		__atomic_fetch_add(&violations, 1u, __ATOMIC_RELAXED);
		__atomic_fetch_add(&counts[k], 1u, __ATOMIC_RELAXED);
	}
}

__attribute__((visibility("default"))) void plug_rtInside(int on)
{
	marked = pthread_self();
	__atomic_store_n(&inside, on, __ATOMIC_RELEASE);
}

__attribute__((visibility("default"))) unsigned long long plug_rtViolations(void)
{
	return __atomic_load_n(&violations, __ATOMIC_RELAXED);
}

__attribute__((visibility("default"))) unsigned long long plug_rtWrites(void)
{
	return __atomic_load_n(&writes, __ATOMIC_RELAXED);
}

__attribute__((destructor)) static void report(void)
{
	for (int k = 0; k < RT_N; k++)
		if (counts[k])
			fprintf(stderr, "plug_rtcheck: %s %llu times inside process()\n", names[k], counts[k]);
}

static void *r_malloc(size_t n) { hit(RT_malloc); return malloc(n); }
static void *r_calloc(size_t a, size_t b) { hit(RT_calloc); return calloc(a, b); }
static void *r_realloc(void *p, size_t n) { hit(RT_realloc); return realloc(p, n); }
static void r_free(void *p) { if (p) hit(RT_free); free(p); }
static int r_posix_memalign(void **p, size_t a, size_t n) { hit(RT_posix_memalign); return posix_memalign(p, a, n); }
static int r_mutex_lock(pthread_mutex_t *m) { hit(RT_pthread_mutex_lock); return pthread_mutex_lock(m); }
static int r_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { hit(RT_pthread_cond_wait); return pthread_cond_wait(c, m); }
static int r_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) { hit(RT_pthread_cond_timedwait); return pthread_cond_timedwait(c, m, t); }
static int r_cond_signal(pthread_cond_t *c) { hit(RT_pthread_cond_signal); return pthread_cond_signal(c); }
static int r_cond_broadcast(pthread_cond_t *c) { hit(RT_pthread_cond_broadcast); return pthread_cond_broadcast(c); }
static int r_rdlock(pthread_rwlock_t *l) { hit(RT_pthread_rwlock_rdlock); return pthread_rwlock_rdlock(l); }
static int r_wrlock(pthread_rwlock_t *l) { hit(RT_pthread_rwlock_wrlock); return pthread_rwlock_wrlock(l); }
static void r_unfair_lock(os_unfair_lock_t l) { hit(RT_os_unfair_lock_lock); os_unfair_lock_lock(l); }
static int r_sem_wait(sem_t *s) { hit(RT_sem_wait); return sem_wait(s); }
static long r_dsem_wait(dispatch_semaphore_t s, dispatch_time_t t) { hit(RT_dispatch_semaphore_wait); return dispatch_semaphore_wait(s, t); }
static void r_dispatch_sync(dispatch_queue_t q, dispatch_block_t b) { hit(RT_dispatch_sync); dispatch_sync(q, b); }
static void r_dispatch_async(dispatch_queue_t q, dispatch_block_t b) { hit(RT_dispatch_async); dispatch_async(q, b); }
static int r_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg) { hit(RT_pthread_create); return pthread_create(t, a, f, arg); }
static int r_pthread_join(pthread_t t, void **v) { hit(RT_pthread_join); return pthread_join(t, v); }
static int r_nanosleep(const struct timespec *a, struct timespec *b) { hit(RT_nanosleep); return nanosleep(a, b); }
static int r_usleep(useconds_t u) { hit(RT_usleep); return usleep(u); }
static unsigned r_sleep(unsigned s) { hit(RT_sleep); return sleep(s); }
static int r_sched_yield(void) { hit(RT_sched_yield); return sched_yield(); }
static ssize_t r_read(int fd, void *p, size_t n) { hit(RT_read); return read(fd, p, n); }
static int r_poll(struct pollfd *f, nfds_t n, int t) { hit(RT_poll); return poll(f, n, t); }
static int r_select(int n, fd_set *a, fd_set *b, fd_set *c, struct timeval *t) { hit(RT_select); return select(n, a, b, c, t); }
/* the variadic ones: the optional argument is read as one word, as libc's own wrappers do */
static int r_open(const char *path, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	const int mode = (flags & O_CREAT) ? va_arg(ap, int) : 0;
	va_end(ap);
	hit(RT_open);
	return open(path, flags, mode);
}
static int r_openat(int dir, const char *path, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	const int mode = (flags & O_CREAT) ? va_arg(ap, int) : 0;
	va_end(ap);
	hit(RT_openat);
	return openat(dir, path, flags, mode);
}
static int r_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	va_start(ap, cmd);
	void *arg = va_arg(ap, void *);
	va_end(ap);
	hit(RT_fcntl);
	return fcntl(fd, cmd, arg);
}
static int r_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	va_start(ap, req);
	void *arg = va_arg(ap, void *);
	va_end(ap);
	hit(RT_ioctl);
	return ioctl(fd, req, arg);
}
static int r_close(int fd) { hit(RT_close); return close(fd); }
static off_t r_lseek(int fd, off_t o, int w) { hit(RT_lseek); return lseek(fd, o, w); }
static int r_stat(const char *p, struct stat *s) { hit(RT_stat); return stat(p, s); }
static int r_fstat(int fd, struct stat *s) { hit(RT_fstat); return fstat(fd, s); }
static void *r_mmap(void *a, size_t n, int prot, int fl, int fd, off_t o) { hit(RT_mmap); return mmap(a, n, prot, fl, fd, o); }
static int r_munmap(void *a, size_t n) { hit(RT_munmap); return munmap(a, n); }
static int r_mprotect(void *a, size_t n, int prot) { hit(RT_mprotect); return mprotect(a, n, prot); }
static pid_t r_getpid(void) { hit(RT_getpid); return getpid(); }
static pid_t r_getppid(void) { hit(RT_getppid); return getppid(); }
static int r_kill(pid_t p, int sig) { hit(RT_kill); return kill(p, sig); }
static pid_t r_fork(void) { hit(RT_fork); return fork(); }
static int r_posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at,
	char *const argv[], char *const envp[])
{
	hit(RT_posix_spawn);
	return posix_spawn(pid, path, fa, at, argv, envp);
}
static int r_posix_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at,
	char *const argv[], char *const envp[])
{
	hit(RT_posix_spawnp);
	return posix_spawnp(pid, file, fa, at, argv, envp);
}
static pid_t r_waitpid(pid_t p, int *st, int o) { hit(RT_waitpid); return waitpid(p, st, o); }
static ssize_t r_write(int fd, const void *p, size_t n)
{
	if (marking())
		__atomic_fetch_add(&writes, 1u, __ATOMIC_RELAXED);
	return write(fd, p, n);
}

#define INTERPOSE(rep, orig) \
	__attribute__((used)) static const struct { const void *r; const void *o; } interpose_##rep \
	__attribute__((section("__DATA,__interpose"))) = { (const void *)&rep, (const void *)&orig }

INTERPOSE(r_malloc, malloc);
INTERPOSE(r_calloc, calloc);
INTERPOSE(r_realloc, realloc);
INTERPOSE(r_free, free);
INTERPOSE(r_posix_memalign, posix_memalign);
INTERPOSE(r_mutex_lock, pthread_mutex_lock);
INTERPOSE(r_cond_wait, pthread_cond_wait);
INTERPOSE(r_cond_timedwait, pthread_cond_timedwait);
INTERPOSE(r_cond_signal, pthread_cond_signal);
INTERPOSE(r_cond_broadcast, pthread_cond_broadcast);
INTERPOSE(r_rdlock, pthread_rwlock_rdlock);
INTERPOSE(r_wrlock, pthread_rwlock_wrlock);
INTERPOSE(r_unfair_lock, os_unfair_lock_lock);
INTERPOSE(r_sem_wait, sem_wait);
INTERPOSE(r_dsem_wait, dispatch_semaphore_wait);
INTERPOSE(r_dispatch_sync, dispatch_sync);
INTERPOSE(r_dispatch_async, dispatch_async);
INTERPOSE(r_pthread_create, pthread_create);
INTERPOSE(r_pthread_join, pthread_join);
INTERPOSE(r_nanosleep, nanosleep);
INTERPOSE(r_usleep, usleep);
INTERPOSE(r_sleep, sleep);
INTERPOSE(r_sched_yield, sched_yield);
INTERPOSE(r_read, read);
INTERPOSE(r_poll, poll);
INTERPOSE(r_select, select);
INTERPOSE(r_open, open);
INTERPOSE(r_openat, openat);
INTERPOSE(r_close, close);
INTERPOSE(r_fcntl, fcntl);
INTERPOSE(r_ioctl, ioctl);
INTERPOSE(r_lseek, lseek);
INTERPOSE(r_stat, stat);
INTERPOSE(r_fstat, fstat);
INTERPOSE(r_mmap, mmap);
INTERPOSE(r_munmap, munmap);
INTERPOSE(r_mprotect, mprotect);
INTERPOSE(r_getpid, getpid);
INTERPOSE(r_getppid, getppid);
INTERPOSE(r_kill, kill);
INTERPOSE(r_fork, fork);
INTERPOSE(r_posix_spawn, posix_spawn);
INTERPOSE(r_posix_spawnp, posix_spawnp);
INTERPOSE(r_waitpid, waitpid);
INTERPOSE(r_write, write);
