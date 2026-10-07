/* A tiny self-contained assert harness for the sfvr tests: no dependencies.
 * Each test file defines static void test_xxx(void) functions, lists them in
 * main() with RUN(), and returns SFVR_TEST_RESULT(). Failures print the file and
 * line and do not stop the run. */
#ifndef SFVR_TEST_H
#define SFVR_TEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int sfvr_test_checks, sfvr_test_failures, sfvr_test_cases;

#define CHECK(cond) \
	do { \
		sfvr_test_checks++; \
		if (!(cond)) { \
			sfvr_test_failures++; \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_INT(actual, expected) \
	do { \
		long a_ = (long)(actual), e_ = (long)(expected); \
		sfvr_test_checks++; \
		if (a_ != e_) { \
			sfvr_test_failures++; \
			printf("  FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #actual, a_, e_); \
		} \
	} while (0)

#define CHECK_NEAR(actual, expected, eps) \
	do { \
		double a_ = (double)(actual), e_ = (double)(expected); \
		sfvr_test_checks++; \
		if (!(fabs(a_ - e_) <= (double)(eps))) { \
			sfvr_test_failures++; \
			printf("  FAIL %s:%d: %s == %.6f, expected %.6f\n", __FILE__, __LINE__, #actual, a_, e_); \
		} \
	} while (0)

#define CHECK_STR(actual, expected) \
	do { \
		const char *a_ = (actual), *e_ = (expected); \
		sfvr_test_checks++; \
		if (strcmp(a_, e_) != 0) { \
			sfvr_test_failures++; \
			printf("  FAIL %s:%d: %s\n    got      \"%s\"\n    expected \"%s\"\n", __FILE__, __LINE__, #actual, a_, e_); \
		} \
	} while (0)

#define RUN(fn) \
	do { \
		int before_ = sfvr_test_failures; \
		sfvr_test_cases++; \
		fn(); \
		if (sfvr_test_failures != before_) \
			printf("  ^ in %s\n", #fn); \
	} while (0)

#define SFVR_TEST_RESULT(name) \
	(printf("%s: %d cases, %d checks, %d failures\n", name, sfvr_test_cases, sfvr_test_checks, sfvr_test_failures), \
		sfvr_test_failures ? 1 : 0)

#endif
