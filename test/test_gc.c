#include "testutil.h"
#include "rt.h"

#include <stdlib.h>

struct suite_data {
    struct rt_task task;

    uint32_t num_freed;
    uint32_t max_freed;
    void **freed;

    struct rt_type **typelist_any;
};

static void free_func(void *userdata, void *ptr) {
    struct suite_data *data = userdata;
    if (data->num_freed == data->max_freed) {
        data->max_freed = data->max_freed ? data->max_freed * 2 : 16;
        data->freed = realloc(data->freed, sizeof(void *) * data->max_freed);
    }
    data->freed[data->num_freed++] = (char *)ptr + sizeof(struct rt_box);
    free(ptr);
}

static void setup(struct test_context *tc) {
    struct suite_data *data = tc->suite_data;

    memset(&data->task, 0, sizeof(struct rt_task));
    data->task.free_func = free_func;
    data->task.free_func_userdata = data;

    data->num_freed = 0;
}

static void teardown(struct test_context *tc) {
    struct suite_data *data = tc->suite_data;
    rt_task_cleanup(&data->task);
}



static void require_that_simple_unreferenced_is_collected(struct test_context *tc) {
    struct suite_data *data = tc->suite_data;
    void *ptr = rt_new_cons(&data->task, rt_nil, rt_nil).u.cons;
    rt_gc_run(&data->task);
    TEST_ASSERT(tc, data->num_freed == 1);
    TEST_ASSERT(tc, data->freed[0] == ptr);
}

static void require_that_simple_referenced_is_not_collected(struct test_context *tc) {
    struct suite_data *data = tc->suite_data;
    struct rt_any cons = rt_new_cons(&data->task, rt_nil, rt_nil);
    void *roots[] = { data->task.roots, data->typelist_any, &cons };
    data->task.roots = roots;
    rt_gc_run(&data->task);
    TEST_ASSERT(tc, data->num_freed == 0);
}



TEST_SUITE_BEGIN(gc_test_suite, setup, teardown)
{
    rt_init();
    struct suite_data *data = calloc(1, sizeof(struct suite_data));
    data->typelist_any = calloc(1, sizeof(void *) * 2);
    data->typelist_any[0] = rt_types.any;
    tc->suite_data = data;
}
TEST_SUITE_TEST(require_that_simple_unreferenced_is_collected)
TEST_SUITE_TEST(require_that_simple_referenced_is_not_collected)
{
    struct suite_data *data = tc->suite_data;
    rt_task_cleanup(&data->task);
    free(data->typelist_any);
    free(data->freed);
    free(data);
    rt_cleanup();
}
TEST_SUITE_END()
