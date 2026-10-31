/*
 * app_time.c
 *
 * Local time for the names of backups and crash notes (stage 1b spec 6.7, 11, plan
 * E17, E20): days-from-civil arithmetic, and a UTC offset that the main thread
 * stores, because localtime_r() is not async-signal-safe.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "app_time.h"

#include <stdatomic.h>
#include <time.h>

static _Atomic int64_t app_utcOffset;

void app_civil(int64_t days, int *y, unsigned *m, unsigned *d)
{
	/* the inverse of days-from-civil (H. Hinnant's algorithm), proleptic Gregorian */
	const int64_t z = days + 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
	const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
	const unsigned mp = (5u * doy + 2u) / 153u;
	*d = doy - (153u * mp + 2u) / 5u + 1u;
	*m = mp < 10u ? mp + 3u : mp - 9u;
	*y = (int)((int64_t)yoe + era * 400 + (*m <= 2u));
}

size_t app_stampName(int64_t localSec, const char *prefix, const char *suffix, char *out, size_t n)
{
	int64_t days = localSec / 86400, s = localSec % 86400;
	if (s < 0)
	{
		s += 86400;
		days--;
	}
	int y;
	unsigned mo, d;
	app_civil(days, &y, &mo, &d);
	unsigned v[6] = { (unsigned)y, mo, d, (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60) };
	static const unsigned width[6] = { 4u, 2u, 2u, 2u, 2u, 2u };
	size_t k = 0;
	for (const char *p = prefix; *p; p++)
	{
		if (k + 1u >= n)
			return 0;
		out[k++] = *p;
	}
	for (unsigned i = 0; i < 6u; i++)
	{
		if (i == 3u && k + 1u < n)
			out[k++] = '-';
		if (k + width[i] >= n)
			return 0;
		for (unsigned j = width[i]; j-- > 0u; v[i] /= 10u)
			out[k + j] = (char)('0' + v[i] % 10u);
		k += width[i];
	}
	for (const char *p = suffix; *p; p++)
	{
		if (k + 1u >= n)
			return 0;
		out[k++] = *p;
	}
	out[k] = '\0';
	return k;
}

void app_timeZoneRefresh(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	const time_t t = ts.tv_sec;
	struct tm tm;
	if (localtime_r(&t, &tm))
		atomic_store(&app_utcOffset, (int64_t)tm.tm_gmtoff);
}

int64_t app_localSeconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec + atomic_load(&app_utcOffset);
}
