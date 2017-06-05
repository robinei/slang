#include <stdlib.h>
#include <stdio.h>

#include "rt.h"

#include "hashtable.h"
#include "murmur3.h"



static uint32_t int_hash(int key) {
    return key;
}

static int int_equals(int a, int b) {
    return a == b;
}

DECL_HASH_TABLE(hash_table, int, int)
IMPL_HASH_TABLE(hash_table, int, int, int_hash, int_equals)


int main(int argc, char *argv[]) {
    struct hash_table table = {0,};
    hash_table_init(&table, 16);
    hash_table_put(&table, 7, 123);
    int val;
    if (hash_table_get(&table, 7, &val)) {
        printf("found %d\n", val);
    }
    hash_table_clear(&table);


    struct rt_thread_ctx ctx = {0,};

    rt_init_types();

    struct rt_any x = rt_new_string_from_cstring(&ctx, "foo");
    struct rt_any y = rt_new_string_from_cstring(&ctx, "bar");
    struct rt_any z = rt_new_string_from_cstring(&ctx, "baz");

    struct rt_any arr = rt_new_array(&ctx, 10, rt_gettype_boxed_array(rt_types.any, 0));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = z;

    struct rt_any cons = rt_new_cons(&ctx, y, rt_new_cons(&ctx, rt_new_cons(&ctx, rt_new_u8(1), rt_new_u8(2)), rt_new_cons(&ctx, z, rt_nil)));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = cons;
    rt_box_array_ref(arr.u.ptr, struct rt_any, 2) = rt_weak_any(cons);

    rt_box_array_ref(arr.u.ptr, struct rt_any, 3) = rt_new_b32(FALSE);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 4) = rt_new_u8(99);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 5) = rt_new_f64(4.67);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 6) = rt_new_cons(&ctx, rt_nil, rt_nil);

    rt_print_any(arr); printf("\n");

    struct rt_type *types[4] = { rt_types.any, rt_types.any, rt_types.any, NULL };
    void *roots[5] = { ctx.roots, types, &x, &y, &arr };
    ctx.roots = roots;
    rt_gc_run(&ctx);

    printf("-\n");
    rt_print_any(arr); printf("\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = rt_nil;
    rt_cdr(cons.u.ptr) = rt_nil;
    rt_gc_run(&ctx);

    printf("-\n");
    rt_print_any(arr); printf("\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = rt_nil;
    rt_gc_run(&ctx);

    printf("-\n");
    rt_print_any(arr); printf("\n");

    ctx.roots = roots[0];
    rt_gc_run(&ctx);
}

