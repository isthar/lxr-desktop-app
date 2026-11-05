/*
 * plug_rtcheck.c
 *
 * libplug_rtcheck.dylib, the real-time check of the plugin's audio thread (plan E9.2 Task 9):
 * loaded into lichomat-plughost with DYLD_INSERT_LIBRARIES, it interposes the calls an audio
 * thread must not make (allocation, locks, waits, file and pipe reads, sleeps) and counts those
 * made while the host has marked its thread as inside process() (plug_rtInside()). write() is
 * counted apart: the plugin makes one, non-blocking, to wake its engine. Each forbidden function
 * that was called is printed at exit.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <dispatch/dispatch.h>
#include <os/lock.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/* the marked thread; no thread-local variable: their first use allocates, which recurses here */
static pthread_t marked;
static int inside;
static unsigned long long violations, writes;
static unsigned long long counts[16];
static const char *const names[16] = { "malloc", "calloc", "realloc", "free", "pthread_mutex_lock", "pthread_cond_wait",
	"pthread_cond_timedwait", "os_unfair_lock_lock", "sem_wait", "nanosleep", "usleep", "read", "poll", "select",
	"dispatch_sync", "pthread_rwlock_rdlock" };

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
	for (int k = 0; k < 16; k++)
		if (counts[k])
			fprintf(stderr, "plug_rtcheck: %s %llu times inside process()\n", names[k], counts[k]);
}

static void *r_malloc(size_t n) { hit(0); return malloc(n); }
static void *r_calloc(size_t a, size_t b) { hit(1); return calloc(a, b); }
static void *r_realloc(void *p, size_t n) { hit(2); return realloc(p, n); }
static void r_free(void *p) { if (p) hit(3); free(p); }
static int r_mutex_lock(pthread_mutex_t *m) { hit(4); return pthread_mutex_lock(m); }
static int r_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { hit(5); return pthread_cond_wait(c, m); }
static int r_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) { hit(6); return pthread_cond_timedwait(c, m, t); }
static void r_unfair_lock(os_unfair_lock_t l) { hit(7); os_unfair_lock_lock(l); }
static int r_sem_wait(sem_t *s) { hit(8); return sem_wait(s); }
static int r_nanosleep(const struct timespec *a, struct timespec *b) { hit(9); return nanosleep(a, b); }
static int r_usleep(useconds_t u) { hit(10); return usleep(u); }
static ssize_t r_read(int fd, void *p, size_t n) { hit(11); return read(fd, p, n); }
static int r_poll(struct pollfd *f, nfds_t n, int t) { hit(12); return poll(f, n, t); }
static int r_select(int n, fd_set *a, fd_set *b, fd_set *c, struct timeval *t) { hit(13); return select(n, a, b, c, t); }
static void r_dispatch_sync(dispatch_queue_t q, dispatch_block_t b) { hit(14); dispatch_sync(q, b); }
static int r_rdlock(pthread_rwlock_t *l) { hit(15); return pthread_rwlock_rdlock(l); }
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
INTERPOSE(r_mutex_lock, pthread_mutex_lock);
INTERPOSE(r_cond_wait, pthread_cond_wait);
INTERPOSE(r_cond_timedwait, pthread_cond_timedwait);
INTERPOSE(r_unfair_lock, os_unfair_lock_lock);
INTERPOSE(r_sem_wait, sem_wait);
INTERPOSE(r_nanosleep, nanosleep);
INTERPOSE(r_usleep, usleep);
INTERPOSE(r_read, read);
INTERPOSE(r_poll, poll);
INTERPOSE(r_select, select);
INTERPOSE(r_dispatch_sync, dispatch_sync);
INTERPOSE(r_rdlock, pthread_rwlock_rdlock);
INTERPOSE(r_write, write);
