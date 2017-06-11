#include <stdlib.h>
#include <stdio.h>

#include "rt.h"

static void print_indent(u32 indent) {
    for (u32 i = 0; i < indent; ++i) {
        putchar(' ');
    }
}

static void print_header(const char *name, struct rt_astnode *node, u32 indent) {
    print_indent(indent); printf("%s: %s\n", name, node->result_type->desc);
}

static void print_ast(struct rt_astnode *node, u32 indent) {
    switch (node->node_type) {
    case RT_ASTNODE_LITERAL: {
        print_header("literal", node, indent);
        if (rt_any_is_ptr(node->const_value) && node->const_value._type->u.ptr.target_type->kind == RT_KIND_FUNC) {
            print_ast(node->const_value.u.func->body_expr, indent + 4);
        } else {
            print_indent(indent + 4); rt_print(node->const_value); printf("\n");
        }
        break;
    }
    case RT_ASTNODE_SCOPE:
        print_header("scope", node, indent);
        print_ast(node->u.scope.expr, indent + 4);
        break;
    case RT_ASTNODE_BLOCK:
        print_header("block", node, indent);
        for (u32 i = 0; i < node->u.block.expr_count; ++i) {
            print_ast(node->u.block.exprs[i], indent + 4);
        }
        break;
    case RT_ASTNODE_GET_GLOBAL:
        print_header("get_global", node, indent);
        break;
    case RT_ASTNODE_GET_LOCAL:
        print_header("get_local", node, indent);
        break;
    case RT_ASTNODE_SET_LOCAL:
        print_header("set_local", node, indent);
        break;
    case RT_ASTNODE_COND:
        print_header("cons", node, indent);
        break;
    case RT_ASTNODE_LOOP:
        print_header("loop", node, indent);
        break;
    case RT_ASTNODE_CALL:
        print_header("call", node, indent);
        break;
    }
}



struct rt_astnode *rt_parse_module(struct rt_task *task, struct rt_any toplevel_module_list);

int main(int argc, char *argv[]) {
    struct rt_task task = {0,};
    struct rt_module mod = {0,};

    rt_init();

    assert(rt_get_symbol("sym").u.ptr == rt_get_symbol("sym").u.ptr);
    assert(rt_any_equals(rt_new_u8(23), rt_new_i64(23)));
    assert(rt_any_equals(rt_get_symbol("sym"), rt_get_symbol("sym")));
    assert(!rt_any_equals(rt_get_symbol("sym"), rt_get_symbol("sym2")));
    assert(rt_lookup_simple_type(rt_get_symbol("u32")) == rt_types.u32);

    struct rt_any x = rt_new_string(&task, "foo");
    struct rt_any y = rt_new_string(&task, "bar");
    struct rt_any z = rt_new_string(&task, "baz");

    struct rt_any arr = rt_new_array(&task, 10, rt_gettype_boxed_array(rt_types.any, 0));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = z;

    struct rt_any cons = rt_new_cons(&task, y, rt_new_cons(&task, rt_new_cons(&task, rt_new_u8(1), rt_new_u8(2)), rt_new_cons(&task, z, rt_nil)));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = cons;
    rt_box_array_ref(arr.u.ptr, struct rt_any, 2) = rt_weak_any(cons);

    rt_box_array_ref(arr.u.ptr, struct rt_any, 3) = rt_new_bool(false);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 4) = rt_new_u8(99);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 5) = rt_new_f64(4.67);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 6) = rt_new_cons(&task, rt_nil, rt_nil);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 6) = rt_get_symbol("sym");
    rt_box_array_ref(arr.u.ptr, struct rt_any, 7) = rt_read(&task, "(foo bar baz)");

    struct rt_any input_form = rt_read(&task, "((def test (fn (x:u32) 1 2 3)))");
    rt_print(input_form); printf("\n");

    struct rt_astnode *node = rt_parse_module(&task, input_form);
    print_ast(node, 0);

    rt_print(arr); printf("\n");

    struct rt_type *types[4] = { rt_types.any, rt_types.any, rt_types.any, NULL };
    void *roots[5] = { task.roots, types, &x, &y, &arr };
    task.roots = roots;
    rt_gc_run(&task);

    printf("-\n");
    rt_print(arr); printf("\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = rt_nil;
    rt_cdr(cons) = rt_nil;
    rt_gc_run(&task);

    printf("-\n");
    rt_print(arr); printf("\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = rt_nil;
    rt_gc_run(&task);

    printf("-\n");
    rt_print(arr); printf("\n");

    task.roots = roots[0];
    rt_gc_run(&task);

    printf("-\n");

    rt_sourcemap_clear(&mod.location_before_car);
    rt_sourcemap_clear(&mod.location_after_car);
    rt_gc_run(&task);

    rt_task_cleanup(&task);
    rt_cleanup();
}

