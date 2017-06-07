#include <stdlib.h>
#include <stdio.h>

#include "rt.h"


int main(int argc, char *argv[]) {
    struct rt_thread_ctx ctx = {0,};
    struct rt_module mod = {0,};

    rt_init_types();

    assert(rt_get_symbol("sym").u.ptr == rt_get_symbol("sym").u.ptr);
    assert(rt_any_equals(rt_new_u8(23), rt_new_i64(23)));
    assert(rt_any_equals(rt_get_symbol("sym"), rt_get_symbol("sym")));
    assert(!rt_any_equals(rt_get_symbol("sym"), rt_get_symbol("sym2")));
    assert(rt_lookup_simple_type(rt_get_symbol("u32")) == rt_types.u32);

    struct rt_any x = rt_new_string(&ctx, "foo");
    struct rt_any y = rt_new_string(&ctx, "bar");
    struct rt_any z = rt_new_string(&ctx, "baz");

    struct rt_any arr = rt_new_array(&ctx, 10, rt_gettype_boxed_array(rt_types.any, 0));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = z;

    struct rt_any cons = rt_new_cons(&ctx, y, rt_new_cons(&ctx, rt_new_cons(&ctx, rt_new_u8(1), rt_new_u8(2)), rt_new_cons(&ctx, z, rt_nil)));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = cons;
    rt_box_array_ref(arr.u.ptr, struct rt_any, 2) = rt_weak_any(cons);

    rt_box_array_ref(arr.u.ptr, struct rt_any, 3) = rt_new_bool(false);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 4) = rt_new_u8(99);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 5) = rt_new_f64(4.67);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 6) = rt_new_cons(&ctx, rt_nil, rt_nil);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 6) = rt_get_symbol("sym");
    rt_box_array_ref(arr.u.ptr, struct rt_any, 7) = rt_read(&ctx, "(foo bar baz)");

    struct rt_any parent_form = rt_read(&ctx, "((array u32 8))");
    rt_print(parent_form); printf("\n");
    struct rt_type *type = rt_parse_type(&ctx, parent_form, rt_car(parent_form.u.ptr));
    printf("type desc: %s\n", type->desc);

    struct rt_any input_form = rt_read(&ctx, "(fun test (x:u32 y:cons) (+ x[0] y.car))");
    rt_print(input_form); printf("\n");

    rt_print(arr); printf("\n");

    struct rt_type *types[4] = { rt_types.any, rt_types.any, rt_types.any, NULL };
    void *roots[5] = { ctx.roots, types, &x, &y, &arr };
    ctx.roots = roots;
    rt_gc_run(&ctx);

    printf("-\n");
    rt_print(arr); printf("\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = rt_nil;
    rt_cdr(cons.u.ptr) = rt_nil;
    rt_gc_run(&ctx);

    printf("-\n");
    rt_print(arr); printf("\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = rt_nil;
    rt_gc_run(&ctx);

    printf("-\n");
    rt_print(arr); printf("\n");

    ctx.roots = roots[0];
    rt_gc_run(&ctx);

    printf("-\n");

    rt_sourcemap_clear(&mod.sourcemap);
    rt_gc_run(&ctx);
}

