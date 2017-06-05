#include "rt.h"

#include <string.h>

struct rt_type *rt_gettype_simple(enum rt_kind kind, rt_size_t size) {
    struct rt_type *existing = rt_types.types_simple;
    while (existing) {
        if (existing->kind == kind && existing->size == size) {
            return existing;
        }
        existing = existing->next;
    }
    struct rt_type *new_type = calloc(1, sizeof(struct rt_type));
    new_type->kind = kind;
    new_type->size = size;
    new_type->next = rt_types.types_simple;
    rt_types.types_simple = new_type;
    return new_type;
}

struct rt_type *rt_gettype_ptr(struct rt_type *target_type) {
    struct rt_type *existing = rt_types.types_ptr;
    while (existing) {
        if (existing->u.ptr.target_type == target_type) {
            return existing;
        }
        existing = existing->next;
    }
    struct rt_type *new_type = calloc(1, sizeof(struct rt_type));
    new_type->kind = RT_KIND_PTR;
    new_type->size = sizeof(void *);
    new_type->u.ptr.target_type = target_type;
    new_type->next = rt_types.types_ptr;
    rt_types.types_ptr = new_type;
    return new_type;
}

struct rt_type *rt_gettype_boxptr(struct rt_type *target_type, struct rt_type *box_type, rt_size_t box_offset) {
    struct rt_type *existing = rt_types.types_boxptr;
    while (existing) {
        if (existing->u.ptr.target_type == target_type &&
            existing->u.ptr.box_type == box_type &&
            existing->u.ptr.box_offset == box_offset) {
            return existing;
        }
        existing = existing->next;
    }
    struct rt_type *new_type = calloc(1, sizeof(struct rt_type));
    new_type->kind = RT_KIND_PTR;
    new_type->size = sizeof(void *);
    new_type->u.ptr.target_type = target_type;
    new_type->u.ptr.box_type = box_type;
    new_type->u.ptr.box_offset = box_offset;
    new_type->next = rt_types.types_boxptr;
    rt_types.types_boxptr = new_type;
    return new_type;
}

struct rt_type *rt_gettype_boxed(struct rt_type *target_type) {
    return rt_gettype_boxptr(target_type, target_type, 0);
}

struct rt_type *rt_gettype_weakptr(struct rt_type *ptr_type) {
    assert(ptr_type->kind == RT_KIND_PTR);
    assert(ptr_type->u.ptr.box_type);
    struct rt_type *existing = rt_types.types_weakptr;
    while (existing) {
        if (existing->u.ptr.target_type == ptr_type->u.ptr.target_type) {
            return existing;
        }
        existing = existing->next;
    }
    struct rt_type *new_type = calloc(1, sizeof(struct rt_type));
    new_type->kind = RT_KIND_PTR;
    new_type->flags = RT_TYPE_FLAG_WEAK_PTR;
    new_type->size = sizeof(void *);
    new_type->u.ptr.target_type = ptr_type->u.ptr.target_type;
    new_type->u.ptr.box_type = ptr_type->u.ptr.box_type;
    new_type->u.ptr.box_offset = ptr_type->u.ptr.box_offset;
    new_type->next = rt_types.types_weakptr;
    rt_types.types_weakptr = new_type;
    return new_type;
}

struct rt_type *rt_gettype_weakptr_boxed(struct rt_type *target_type) {
    return rt_gettype_weakptr(rt_gettype_boxed(target_type));
}

struct rt_type *rt_gettype_array(struct rt_type *elem_type, rt_size_t length) {
    assert(elem_type->size);
    rt_size_t size = length ? elem_type->size*length : 0;
    struct rt_type *existing = rt_types.types_array;
    while (existing) {
        if (existing->size == size && existing->u.array.elem_type == elem_type) {
            return existing;
        }
        existing = existing->next;
    }
    struct rt_type *new_type = calloc(1, sizeof(struct rt_type));
    new_type->kind = RT_KIND_ARRAY;
    new_type->size = size;
    new_type->u.array.elem_type = elem_type;
    new_type->next = rt_types.types_array;
    rt_types.types_array = new_type;
    return new_type;
}

struct rt_type *rt_gettype_boxed_array(struct rt_type *elem_type, rt_size_t length) {
    return rt_gettype_boxed(rt_gettype_array(elem_type, length));
}

struct rt_type *rt_gettype_struct(rt_size_t size, u32 field_count, struct rt_struct_field *fields) {
    struct rt_type *existing = rt_types.types_struct;
    while (existing) {
        if (existing->size == size && existing->u._struct.field_count == field_count) {
            b32 fields_same = TRUE;
            for (u32 i = 0; i < field_count; ++i) {
                struct rt_struct_field *f1 = existing->u._struct.fields + i;
                struct rt_struct_field *f2 = fields + i;
                if (f1->type != f2->type || strcmp(f1->name, f2->name) || f1->offset != f2->offset) {
                    fields_same = FALSE;
                    break;
                }
            }
            if (fields_same) {
                return existing;
            }
        }
        existing = existing->next;
    }
#ifndef NDEBUG
    for (u32 i = 0; i < field_count - 1; ++i) {
        struct rt_struct_field *f = fields + i;
        assert(f->type->size);
    }
    if (field_count) {
        if (size) {
            assert(fields[field_count - 1].type->size != 0);
        } else {
            assert(fields[field_count - 1].type->size == 0);
        }
    } else {
        assert(size == 0);
    }
#endif
    struct rt_struct_field *new_fields = malloc(sizeof(struct rt_struct_field) * field_count);
    memcpy(new_fields, fields, sizeof(struct rt_struct_field) * field_count);
    struct rt_type *new_type = calloc(1, sizeof(struct rt_type));
    new_type->kind = RT_KIND_STRUCT;
    new_type->size = size;
    new_type->u._struct.field_count = field_count;
    new_type->u._struct.fields = new_fields;
    new_type->next = rt_types.types_struct;
    rt_types.types_struct = new_type;
    return new_type;
}
