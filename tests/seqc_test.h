/*
 * seqc_test.h
 *
 * Minimal test runner for the host tests. One test file per executable:
 *
 *   TEST(T2_straight_grid) { ASSERT_EQ(n, 16); ASSERT_NEAR(bpm, 130.0, 1e-3); }
 *   TEST_MAIN()
 *
 * "<exe>" runs every test, "<exe> T2" only the tests named T2 or T2_... (ctest
 * passes the spec test id this way). The exit status is 1 if a test failed and 2
 * if no test matches. An ASSERT_ that fails prints file:line and returns from the
 * function it is in, so use it in a TEST body or in a void helper. ASSERT_EQ
 * compares integers (and enums) as long long; use ASSERT_NEAR for floating point.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#ifndef SEQC_TEST_H_
#define SEQC_TEST_H_

#include <stdio.h>
#include <string.h>

#define SEQC_TEST_MAX 256
typedef struct { const char *name; void (*fn)(void); } seqc_test_case_t;
__attribute__((unused)) static seqc_test_case_t seqc_test_cases_[SEQC_TEST_MAX];
__attribute__((unused)) static int seqc_test_count_;	/* registered, may exceed SEQC_TEST_MAX */
__attribute__((unused)) static int seqc_test_failed_;	/* the running test has failed */

/* each TEST registers itself before main() runs, in source order */
#define TEST(name) \
	static void seqc_tfn_##name(void); \
	__attribute__((constructor)) static void seqc_treg_##name(void) { \
		if (seqc_test_count_ < SEQC_TEST_MAX) \
			seqc_test_cases_[seqc_test_count_] = (seqc_test_case_t){ #name, seqc_tfn_##name }; \
		seqc_test_count_++; } \
	static void seqc_tfn_##name(void)

#define SEQC_TEST_FAIL_(...) do { printf("%s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
	printf("\n"); seqc_test_failed_ = 1; return; } while (0)
#define ASSERT_TRUE(c) do { if (!(c)) SEQC_TEST_FAIL_("ASSERT_TRUE(%s) failed", #c); } while (0)
#define ASSERT_FALSE(c) do { if (c) SEQC_TEST_FAIL_("ASSERT_FALSE(%s) failed", #c); } while (0)
#define ASSERT_EQ(a, b) do { long long a_ = (long long)(a), b_ = (long long)(b); if (a_ != b_) \
	SEQC_TEST_FAIL_("ASSERT_EQ(%s, %s) failed: %lld != %lld", #a, #b, a_, b_); } while (0)
#define ASSERT_NEAR(a, b, tol) do { double a_ = (double)(a), b_ = (double)(b), t_ = (double)(tol); \
	if (!(a_ - b_ <= t_ && b_ - a_ <= t_)) SEQC_TEST_FAIL_("ASSERT_NEAR(%s, %s, %s) failed: %.9g vs %.9g", \
	#a, #b, #tol, a_, b_); } while (0)

/* a filter selects the test of that name and the tests named <filter>_... */
__attribute__((unused)) static int seqc_test_matches_(const char *name, const char *filter)
{
	const size_t n = strlen(filter);
	return strncmp(name, filter, n) == 0 && (name[n] == '\0' || name[n] == '_');
}

#define TEST_MAIN() \
	int main(int argc, char **argv) \
	{ \
		const char *filter = argc > 1 ? argv[1] : NULL; \
		int ran = 0, failed = 0; \
		if (seqc_test_count_ > SEQC_TEST_MAX) { printf("more than %d tests\n", SEQC_TEST_MAX); return 2; } \
		for (int i = 0; i < seqc_test_count_; i++) { \
			if (filter && !seqc_test_matches_(seqc_test_cases_[i].name, filter)) continue; \
			seqc_test_failed_ = 0; \
			seqc_test_cases_[i].fn(); \
			ran++; failed += seqc_test_failed_; \
			printf("%s %s\n", seqc_test_failed_ ? "FAIL" : "PASS", seqc_test_cases_[i].name); \
			fflush(stdout); \
		} \
		if (ran == 0) { printf("no test matches %s\n", filter ? filter : "(none)"); return 2; } \
		printf("%d of %d tests passed\n", ran - failed, ran); \
		return failed ? 1 : 0; \
	}

#endif /* SEQC_TEST_H_ */
