#include "rt.h"

#include <inttypes.h>
#include <stdio.h>

static void rt_print_ptr(char *ptr, struct rt_type *type);

static void rt_print_cons(struct rt_cons *cons) {
    if (!cons) {
        printf("nil"); // TODO: make weak references also reset the type for rt_any cells
        return;
    }
    bool first = true;
    printf("(");
    while (cons) {
        if (!first) {
            printf(" ");
        }
        first = false;
        rt_print(cons->car);
        if (rt_any_is_nil(cons->cdr)) {
            break;
        }
        if (!rt_any_is_cons(cons->cdr)) {
            printf(" . ");
            rt_print(cons->cdr);
            break;
        }
        cons = (struct rt_cons *)cons->cdr.u.ptr;
    }
    printf(")");
}

static void rt_print_struct(char *ptr, struct rt_type *type) {
    if (type == rt_types.string) {
        struct rt_string *string = (struct rt_string *)ptr;
        printf("\"%s\"", string->chars.data);
        return;
    }
    if (type == rt_types.symbol) {
        struct rt_symbol *sym = (struct rt_symbol *)ptr;
        printf("%s", sym->string.chars.data);
        return;
    }
    if (type == rt_types.cons) {
        rt_print_cons((struct rt_cons *)ptr);
        return;
    }
    printf("{");
    u32 field_count = type->u._struct.field_count;
    for (u32 i = 0; i < field_count; ++i) {
        struct rt_struct_field *f = type->u._struct.fields + i;
        printf("%s: ", f->name);
        rt_print_ptr(ptr + f->offset, f->type);
        if (i != field_count - 1) {
            printf(", ");
        }
    }
    printf("}");
}

static void rt_print_array(char *ptr, struct rt_type *type) {
    printf("[");
    struct rt_type *elem_type = type->u.array.elem_type;
    rt_size_t elem_size = elem_type->size;
    rt_size_t length;
    if (type->size) {
        length = type->size / elem_size;
    } else {
        /* an unsized array starts with a length */
        length = *(rt_size_t *)ptr;
        ptr += sizeof(rt_size_t);
    }
    for (rt_size_t i = 0; i < length; ++i) {
        rt_print_ptr(ptr + i*elem_size, elem_type);
        if (i != length - 1) {
            printf(" ");
        }
    }
    printf("]");
}

static void rt_print_ptr(char *ptr, struct rt_type *type) {
    switch (type->kind) {
    case RT_KIND_ANY: {
        struct rt_any *any = (struct rt_any *)ptr;
        rt_print_ptr((char *)&any->u.data, rt_any_get_type(*any));
        break;
    }
    case RT_KIND_NIL:
        printf("nil");
        break;
    case RT_KIND_PTR:
        rt_print_ptr(*(char **)ptr, type->u.ptr.target_type);
        break;
    case RT_KIND_STRUCT: {
        rt_print_struct(ptr, type);
        break;
    }
    case RT_KIND_ARRAY: {
        rt_print_array(ptr, type);
        break;
    }
    case RT_KIND_BOOL:
        printf("%s", *(bool *)ptr ? "true" : "false"); break;
        break;
    case RT_KIND_SIGNED:
        switch (type->size) {
        case 1: printf("%"PRIi8, *(i8 *)ptr); break;
        case 2: printf("%"PRIi16, *(i16 *)ptr); break;
        case 4: printf("%"PRIi32, *(i32 *)ptr); break;
        case 8: printf("%"PRIi64, *(i64 *)ptr); break;
        }
        break;
    case RT_KIND_UNSIGNED:
        switch (type->size) {
        case 1: printf("%"PRIu8, *(u8 *)ptr); break;
        case 2: printf("%"PRIu16, *(u16 *)ptr); break;
        case 4: printf("%"PRIu32, *(u32 *)ptr); break;
        case 8: printf("%"PRIu64, *(u64 *)ptr); break;
        }
        break;
    case RT_KIND_REAL:
        switch (type->size) {
        case 4: printf("%f", *(f32 *)ptr); break;
        case 8: printf("%f", *(f64 *)ptr); break;
        }
        break;
    }
}

void rt_print(struct rt_any any) {
    rt_print_ptr((char *)&any.u.data, rt_any_get_type(any));
}
