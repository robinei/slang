#include <stdlib.h>
#include <stdio.h>

#include "rt.h"


int main(int argc, char *argv[]) {
    struct rt_thread_ctx ctx = {0,};

    rt_init_types();

    assert(rt_any_equals(rt_new_u8(23), rt_new_i64(23)));
    assert(rt_any_equals(rt_get_symbol("sym"), rt_get_symbol("sym")));
    assert(!rt_any_equals(rt_get_symbol("sym"), rt_get_symbol("sym2")));

    struct rt_any x = rt_new_string(&ctx, "foo");
    struct rt_any y = rt_new_string(&ctx, "bar");
    struct rt_any z = rt_new_string(&ctx, "baz");

    struct rt_any arr = rt_new_array(&ctx, 10, rt_gettype_boxed_array(rt_types.any, 0));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = z;

    struct rt_any cons = rt_new_cons(&ctx, y, rt_new_cons(&ctx, rt_new_cons(&ctx, rt_new_u8(1), rt_new_u8(2)), rt_new_cons(&ctx, z, rt_nil)));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = cons;
    rt_box_array_ref(arr.u.ptr, struct rt_any, 2) = rt_weak_any(cons);

    rt_box_array_ref(arr.u.ptr, struct rt_any, 3) = rt_new_b32(FALSE);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 4) = rt_new_u8(99);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 5) = rt_new_f64(4.67);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 6) = rt_new_cons(&ctx, rt_nil, rt_nil);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 6) = rt_get_symbol("sym");

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

