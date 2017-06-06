#include "rt.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

struct parse_state {
    struct rt_thread_ctx *ctx;
};

static void parse_error(struct rt_thread_ctx *ctx, struct rt_any form, const char *fmt, ...) {
    struct rt_sourceloc loc;
    if (rt_any_is_cons(form) && rt_sourcemap_get(&ctx->sourcemap, form.u.cons, &loc)) {
        printf("line %d, col %d: ", loc.line + 1, loc.col + 1);
    } else {
        printf("line ?, col ?: ");
    }
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    exit(1);
}

struct rt_type *rt_parse_type(struct rt_thread_ctx *ctx, struct rt_any parent_form, struct rt_any form) {
    if (!rt_any_is_cons(form)) {
        if (!rt_any_is_symbol(form)) {
            parse_error(ctx, parent_form, "invalid type");
            return NULL;
        }

        struct rt_type *type = rt_lookup_simple_type(form);
        if (type == NULL) {
            parse_error(ctx, parent_form, "invalid type");
            return NULL;
        }
        return type;
    }

    struct rt_any cons1 = form;
    struct rt_any type_sym = rt_car(cons1.u.ptr);
    if (!rt_any_is_symbol(type_sym)) {
        parse_error(ctx, cons1, "invalid type");
        return NULL;
    }

    struct rt_any arr = rt_get_symbol("array");
    if (type_sym.u.ptr == arr.u.ptr) {
        struct rt_any cons2 = rt_cdr(cons1.u.ptr);
        if (!rt_any_is_cons(cons2)) {
            parse_error(ctx, cons1, "invalid array type");
            return NULL;
        }
        struct rt_type *elem_type = rt_parse_type(ctx, cons2, rt_car(cons2.u.ptr));
        struct rt_any cons3 = rt_cdr(cons2.u.ptr);
        if (!rt_any_is_cons(cons3)) {
            if (rt_any_is_nil(cons3)) {
                return rt_gettype_array(elem_type, 0);
            }
            parse_error(ctx, cons2, "invalid array type");
            return NULL;
        }
        struct rt_any elem_count = rt_any_to_unsigned(rt_car(cons3.u.ptr));
        if (!rt_any_is_unsigned(elem_count)) {
            parse_error(ctx, cons3, "invalid array type. expected element count");
            return NULL;
        }
        struct rt_any cons4 = rt_cdr(cons3.u.ptr);
        if (!rt_any_is_nil(cons4)) {
            parse_error(ctx, cons3, "invalid array type");
            return NULL;
        }
        u64 count = rt_any_to_u64(elem_count);
        return rt_gettype_array(elem_type, count);
    }

    parse_error(ctx, cons1, "unrecognized type: %s", ((struct rt_symbol *)form.u.ptr)->string.chars.data);
}
