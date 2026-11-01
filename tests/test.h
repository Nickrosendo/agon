/*
 * The smallest test framework that works: CHECK(cond) prints the file,
 * line and the condition's text when cond is false, and counts the
 * failure. A test program's main ends with TEST_SUMMARY(), which prints a
 * one-line result and returns 1 from main if anything failed.
 *
 * Each tests/test_*.c is its own program with its own main, so this
 * header is included exactly once per program and its static counter is
 * private to that program.
 */
#ifndef AGON_TEST_H
#define AGON_TEST_H

#include <stdio.h>

static int test_checks = 0;
static int test_failures = 0;

/*
 * A macro is pasted in as text before compiling, which is what makes this
 * possible: __FILE__ and __LINE__ become the file and line where CHECK is
 * written, and #cond turns the condition itself into a string.
 * do { ... } while (0) makes the whole thing one statement, so
 * "if (x) CHECK(a); else ..." still works.
 */
#define CHECK(cond)                                                          \
    do {                                                                     \
        test_checks++;                                                       \
        if (!(cond)) {                                                       \
            test_failures++;                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
        }                                                                    \
    } while (0)

#define TEST_SUMMARY()                                                       \
    do {                                                                     \
        printf("%s: %d checks, %d failed\n", __FILE__, test_checks,          \
               test_failures);                                               \
        return test_failures == 0 ? 0 : 1;                                   \
    } while (0)

#endif
