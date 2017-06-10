#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <setjmp.h>
#include <stdio.h>

struct test_context {
    void *suite_data;
    jmp_buf failure_jmp_buf;
};

#define TEST_SUITE_BEGIN(SuiteFunc, SetupFunc, TeardownFunc) \
    void SuiteFunc(struct test_context *tc) { \
        void (*_setup_func)(struct test_context *) = SetupFunc; \
        void (*_teardown_func)(struct test_context *) = TeardownFunc; \
        int passed_count = 0; \
        int failed_count = 0; \
        printf("running test suite %s...\n", #SuiteFunc); \
        tc->suite_data = NULL;

#define TEST_SUITE_TEST(TestFunc) \
        { \
            printf("    %s...", #TestFunc); \
            if (!setjmp(tc->failure_jmp_buf)) { \
                if (_setup_func) { _setup_func(tc); } \
                TestFunc(tc); \
                printf(" pass\n"); \
                ++passed_count; \
            } else { \
                ++failed_count; \
            } \
            if (_teardown_func) { _teardown_func(tc); } \
        }

#define TEST_SUITE_END() \
        printf("    passed: %d, failed: %d\n", passed_count, failed_count); \
    }

#define TEST_ASSERT(TestContext, What) \
    do { \
        if (!(What)) { \
            printf(" FAIL\n"); \
            printf("        (%s:%d) assertion failed: %s\n", __FILE__, __LINE__, #What); \
            longjmp((TestContext)->failure_jmp_buf, 1); \
        } \
    } while(0)

#endif
