/*
 * plug_shm.c
 *
 * The shared memory between the plugin and its engine (plan E9.2 Task 3); see plug_shm.h.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "plug_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(plug_event_t) == 16, "plug_event_t is 16 bytes");
_Static_assert((PLUG_EVENT_SLOTS & (PLUG_EVENT_SLOTS - 1u)) == 0, "PLUG_EVENT_SLOTS is a power of two");
_Static_assert((PLUG_AUDIO_FRAMES & (PLUG_AUDIO_FRAMES - 1u)) == 0, "PLUG_AUDIO_FRAMES is a power of two");
_Static_assert(offsetof(plug_shm_t, hostFrames) % 8 == 0, "the counters are 8-byte aligned");

void plug_shmInit(plug_shm_t *s, double rate, uint32_t maxBlock)
{
	memset(s, 0, sizeof *s);
	s->version = PLUG_SHM_VERSION;
	s->size = (uint32_t)sizeof *s;
	s->maxBlock = maxBlock;
	s->rate = rate;
	__atomic_store_n(&s->magic, PLUG_SHM_MAGIC, __ATOMIC_RELEASE);
}

int plug_shmCheck(const plug_shm_t *s, char *err, size_t n)
{
	if (__atomic_load_n(&s->magic, __ATOMIC_ACQUIRE) != PLUG_SHM_MAGIC)
	{
		snprintf(err, n, "shared memory: no LCHP header");
		return -1;
	}
	if (s->version != PLUG_SHM_VERSION || s->size != sizeof *s)
	{
		snprintf(err, n, "shared memory: version %u size %u, this engine needs version %u size %zu", s->version,
			s->size, PLUG_SHM_VERSION, sizeof *s);
		return -1;
	}
	if (!(s->rate >= 8000.0 && s->rate <= 384000.0) || s->maxBlock == 0 || s->maxBlock > 8192u)
	{
		snprintf(err, n, "shared memory: rate %.3f, block %u", s->rate, s->maxBlock);
		return -1;
	}
	return 0;
}

plug_shm_t *plug_shmMap(int fd, char *err, size_t n)
{
	/* a smaller object is another layout's: say so, as plug_shmCheck() would, rather than fail to
	 * map it (or read past its end) */
	struct stat st;
	if (fstat(fd, &st) != 0)
	{
		snprintf(err, n, "fstat of the shared memory: %s", strerror(errno));
		return NULL;
	}
	if (st.st_size < (off_t)sizeof(plug_shm_t))
	{
		snprintf(err, n, "shared memory: an object of %lld bytes, this engine needs version %u size %zu",
			(long long)st.st_size, PLUG_SHM_VERSION, sizeof(plug_shm_t));
		return NULL;
	}
	void *p = mmap(NULL, sizeof(plug_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
	{
		snprintf(err, n, "mmap of the shared memory: %s", strerror(errno));
		return NULL;
	}
	return p;
}

plug_shm_t *plug_shmCreate(double rate, uint32_t maxBlock, int *fd, char *err, size_t n)
{
	static unsigned counter;
	char name[32];	/* macOS allows 31 characters */
	snprintf(name, sizeof name, "/lchp.%d.%u", (int)getpid(), __atomic_fetch_add(&counter, 1u, __ATOMIC_RELAXED));
	int f = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (f < 0 && errno == EEXIST)
	{
		/* left by a process of this pid that died between shm_open() and shm_unlink(): the name has
		 * this pid, so no live process has it. Taken over once. */
		shm_unlink(name);
		f = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	}
	if (f < 0)
	{
		snprintf(err, n, "shm_open %s: %s", name, strerror(errno));
		return NULL;
	}
	shm_unlink(name);	/* the descriptors keep it */
	if (ftruncate(f, (off_t)sizeof(plug_shm_t)) != 0)
	{
		snprintf(err, n, "ftruncate %s: %s", name, strerror(errno));
		close(f);
		return NULL;
	}
	if (fcntl(f, F_SETFD, FD_CLOEXEC) != 0)
	{
		snprintf(err, n, "fcntl %s: %s", name, strerror(errno));
		close(f);
		return NULL;
	}
	plug_shm_t *s = plug_shmMap(f, err, n);
	if (!s)
	{
		close(f);
		return NULL;
	}
	plug_shmInit(s, rate, maxBlock);
	*fd = f;
	return s;
}

void plug_shmClose(plug_shm_t *s)
{
	if (s)
		munmap(s, sizeof *s);
}

/* --- the plugin's side --------------------------------------------------------------- */

int plug_evPush(plug_shm_t *s, uint64_t frame, const uint8_t *b, uint8_t len)
{
	const uint64_t w = __atomic_load_n(&s->evWrite, __ATOMIC_RELAXED);	/* our own field */
	if (len == 0 || len > 3 || w - plug_load(&s->evRead) >= PLUG_EVENT_SLOTS)
	{
		__atomic_store_n(&s->evDropped, __atomic_load_n(&s->evDropped, __ATOMIC_RELAXED) + 1u, __ATOMIC_RELAXED);
		return -1;
	}
	plug_event_t *e = &s->ev[w & (PLUG_EVENT_SLOTS - 1u)];
	e->frame = frame;
	e->len = len;
	e->b[0] = b[0];
	e->b[1] = len > 1 ? b[1] : 0;
	e->b[2] = len > 2 ? b[2] : 0;
	e->pad = 0;
	plug_store(&s->evWrite, w + 1u);
	return 0;
}

void plug_publish(plug_shm_t *s, uint64_t hostFrames)
{
	plug_store(&s->hostFrames, hostFrames);
}

uint32_t plug_audioRead(plug_shm_t *s, uint64_t from, uint32_t frames, float *l, float *r)
{
	/* audioEnd first: the engine stores audioStart before it, so a new engine's start is seen with
	 * its end or after it, and a range with start > end is empty */
	const uint64_t end = plug_load(&s->audioEnd);
	const uint64_t start = plug_load(&s->audioStart);
	const int running = plug_load(&s->state) == PLUG_ENGINE_RUNNING;
	uint32_t copied = 0, missing = 0;
	for (uint32_t i = 0; i < frames; i++)
	{
		const uint64_t f = from + i;
		/* frame f's slot is also frame f + PLUG_AUDIO_FRAMES's: the ring holds the frames less than
		 * PLUG_AUDIO_FRAMES before end (the one before them shares its slot with frame end, the next
		 * the engine writes) */
		if (f >= start && f < end && end - f < PLUG_AUDIO_FRAMES)
		{
			const float *p = s->audio[f & (PLUG_AUDIO_FRAMES - 1u)];
			l[i] = p[0];
			r[i] = p[1];
			copied++;
		}
		else
		{
			l[i] = r[i] = 0.0f;
			if (running && f >= start && f >= end)
				missing++;
		}
	}
	if (copied)
	{
		/* The engine may have written over the oldest of them while they were copied. It publishes
		 * audioEnd = g before it writes frame g (plug_audioWrite()), so the frames it may have
		 * written over are those with f + PLUG_AUDIO_FRAMES <= audioEnd, loaded again after the copy */
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		const uint64_t now = __atomic_load_n(&s->audioEnd, __ATOMIC_RELAXED);
		for (uint32_t i = 0; i < frames; i++)
		{
			const uint64_t f = from + i;
			if (f >= start && f < end && end - f < PLUG_AUDIO_FRAMES && f + PLUG_AUDIO_FRAMES <= now)
			{
				l[i] = r[i] = 0.0f;
				copied--;
			}
		}
	}
	if (missing)
		__atomic_store_n(&s->underruns, __atomic_load_n(&s->underruns, __ATOMIC_RELAXED) + missing, __ATOMIC_RELAXED);
	return copied;
}

/* --- the engine's side --------------------------------------------------------------- */

int plug_evPop(plug_shm_t *s, plug_event_t *e)
{
	const uint64_t r = __atomic_load_n(&s->evRead, __ATOMIC_RELAXED);
	if (r == plug_load(&s->evWrite))
		return 0;
	*e = s->ev[r & (PLUG_EVENT_SLOTS - 1u)];
	plug_store(&s->evRead, r + 1u);
	return 1;
}

uint64_t plug_evNextFrame(plug_shm_t *s)
{
	const uint64_t r = __atomic_load_n(&s->evRead, __ATOMIC_RELAXED);
	if (r == plug_load(&s->evWrite))
		return UINT64_MAX;
	return s->ev[r & (PLUG_EVENT_SLOTS - 1u)].frame;
}

void plug_evSkipAll(plug_shm_t *s)
{
	plug_store(&s->evRead, plug_load(&s->evWrite));
}

void plug_audioBegin(plug_shm_t *s, uint64_t epoch)
{
	plug_store(&s->audioStart, epoch);
	plug_store(&s->audioEnd, epoch);
}

void plug_audioWrite(plug_shm_t *s, const float *lr, uint32_t frames)
{
	/* Frame by frame: audioEnd = g, published, then frame g over frame g - PLUG_AUDIO_FRAMES, the
	 * release fence ordering the two for plug_audioRead(), which loads audioEnd again after it
	 * copies (an acquire fence) and leaves out what may have been written over meanwhile */
	uint64_t end = __atomic_load_n(&s->audioEnd, __ATOMIC_RELAXED);
	for (uint32_t i = 0; i < frames; i++, end++)
	{
		__atomic_thread_fence(__ATOMIC_RELEASE);
		float *p = s->audio[end & (PLUG_AUDIO_FRAMES - 1u)];
		p[0] = lr[2u * i];
		p[1] = lr[2u * i + 1u];
		plug_store(&s->audioEnd, end + 1u);
	}
}
