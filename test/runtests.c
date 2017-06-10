#include "testutil.h"

void gc_test_suite(struct test_context *);

int main(int argc, char *argv[]) {
    struct test_context tc = {0,};
    gc_test_suite(&tc);
    return 0;
}
