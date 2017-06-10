#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

void rt_gettype_free_all() {
    struct rt_type *type = rt_types.types_all;
    while (type) {
        struct rt_type *next = type->all_list_next;

        if (type->kind == RT_KIND_STRUCT) {
            free(type->u._struct.fields);
        }
        free((char *)type->desc);
        free(type);

        type = next;
    }

    rt_types.types_all = NULL;
    rt_types.types_simple = NULL;
    rt_types.types_ptr = NULL;
    rt_types.types_boxptr = NULL;
    rt_types.types_weakptr = NULL;
    rt_types.types_array = NULL;
    rt_types.types_struct = NULL;
}

static const char *copy_string(const char *str) {
    u32 len = strlen(str);
    char *newstr = malloc(len + 1);
    memcpy(newstr, str, len + 1);
    return newstr;
}

static const char *type_to_string(struct rt_type *type) {
    char buffer[1024];
    u32 len = 0;
    if (!type) {
        return copy_string("nil");
    }
    buffer[0] = 0;
    switch (type->kind) {
    case RT_KIND_ANY: return copy_string("any");
    case RT_KIND_NIL: return copy_string("nil");
    case RT_KIND_PTR:
        len = snprintf(buffer, sizeof(buffer), "ptr[%s]", type->u.ptr.target_type->desc);
        break;
    case RT_KIND_STRUCT:
        if (!type->u._struct.name) {
            return "struct";
        }
        len = snprintf(buffer, sizeof(buffer), "struct %s", type->u._struct.name);
        break;
    case RT_KIND_ARRAY:
        if (type->size) {
            u64 length = type->size / type->u.array.elem_type->size;
            len = snprintf(buffer, sizeof(buffer), "array[%s %"PRIu64"]", type->u.array.elem_type->desc, length);
        } else {
            len = snprintf(buffer, sizeof(buffer), "array[%s]", type->u.array.elem_type->desc);
        }
        break;
    case RT_KIND_BOOL: return copy_string("bool");
    case RT_KIND_SIGNED:
        switch (type->size) {
        case 1: return copy_string("i8");
        case 2: return copy_string("i16");
        case 4: return copy_string("i32");
        case 8: return copy_string("i64");
        }
        break;
    case RT_KIND_UNSIGNED:
        switch (type->size) {
        case 1: return copy_string("u8");
        case 2: return copy_string("u16");
        case 4: return copy_string("u32");
        case 8: return copy_string("u64");
        }
        break;
    case RT_KIND_REAL:
        switch (type->size) {
        case 4: return copy_string("f32");
        case 8: return copy_string("f64");
        }
        break;
    case RT_KIND_FUNC:
        return copy_string("func");
    case RT_KIND_TYPE:
        return copy_string("type");
    }
    if (len) {
        if (len >= sizeof(buffer)) {
            len = sizeof(buffer) - 1;
        }
        char *str = malloc(len + 1);
        memcpy(str, buffer, len + 1);
        return str;
    }
    assert(0 && "type not handled");
}


static struct rt_type *make_type(enum rt_kind kind, rt_size_t size, struct rt_type **list_head) {
    struct rt_type *new_type = calloc(1, sizeof(struct rt_type));
    new_type->kind = kind;
    new_type->size = size;

    new_type->next = *list_head;
    *list_head = new_type;
    
    new_type->all_list_next = rt_types.types_all;
    rt_types.types_all = new_type;
    
    return new_type;
}

struct rt_type *rt_gettype_simple(enum rt_kind kind, rt_size_t size) {
    struct rt_type *existing = rt_types.types_simple;
    while (existing) {
        if (existing->kind == kind && existing->size == size) {
            return existing;
        }
        existing = existing->next;
    }
    struct rt_type *new_type = make_type(kind, size, &rt_types.types_simple);
    new_type->desc = type_to_string(new_type);
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
    struct rt_type *new_type = make_type(RT_KIND_PTR, sizeof(void *), &rt_types.types_ptr);
    new_type->u.ptr.target_type = target_type;
    new_type->desc = type_to_string(new_type);
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
    struct rt_type *new_type = make_type(RT_KIND_PTR, sizeof(void *), &rt_types.types_boxptr);
    new_type->u.ptr.target_type = target_type;
    new_type->u.ptr.box_type = box_type;
    new_type->u.ptr.box_offset = box_offset;
    new_type->desc = type_to_string(new_type);
    return new_type;
}

struct rt_type *rt_gettype_boxed(struct rt_type *target_type) {
    return rt_gettype_boxptr(target_type, target_type, 0);
}

struct rt_type *rt_gettype_weak(struct rt_type *ptr_type) {
    if (ptr_type->flags & RT_TYPE_FLAG_WEAK_PTR) {
        return ptr_type;
    }
    assert(ptr_type->kind == RT_KIND_PTR);
    assert(ptr_type->u.ptr.box_type);
    struct rt_type *existing = rt_types.types_weakptr;
    while (existing) {
        if (existing->u.ptr.target_type == ptr_type->u.ptr.target_type &&
            existing->u.ptr.box_type == ptr_type->u.ptr.box_type &&
            existing->u.ptr.box_offset == ptr_type->u.ptr.box_offset) {
            return existing;
        }
        existing = existing->next;
    }
    struct rt_type *new_type = make_type(RT_KIND_PTR, sizeof(void *), &rt_types.types_weakptr);
    new_type->flags = RT_TYPE_FLAG_WEAK_PTR;
    new_type->u.ptr.target_type = ptr_type->u.ptr.target_type;
    new_type->u.ptr.box_type = ptr_type->u.ptr.box_type;
    new_type->u.ptr.box_offset = ptr_type->u.ptr.box_offset;
    new_type->desc = type_to_string(new_type);
    return new_type;
}

struct rt_type *rt_gettype_weak_boxed(struct rt_type *target_type) {
    return rt_gettype_weak(rt_gettype_boxed(target_type));
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
    struct rt_type *new_type = make_type(RT_KIND_ARRAY, size, &rt_types.types_array);
    new_type->u.array.elem_type = elem_type;
    new_type->desc = type_to_string(new_type);
    return new_type;
}

struct rt_type *rt_gettype_boxed_array(struct rt_type *elem_type, rt_size_t length) {
    return rt_gettype_boxed(rt_gettype_array(elem_type, length));
}

struct rt_type *rt_gettype_struct(const char *name, rt_size_t size, u32 field_count, struct rt_struct_field *fields) {
    struct rt_type *existing = rt_types.types_struct;
    while (existing) {
        if (existing->size == size && existing->u._struct.field_count == field_count) {
            bool fields_same = true;
            for (u32 i = 0; i < field_count; ++i) {
                struct rt_struct_field *f1 = existing->u._struct.fields + i;
                struct rt_struct_field *f2 = fields + i;
                if (f1->type != f2->type || strcmp(f1->name, f2->name) || f1->offset != f2->offset) {
                    fields_same = false;
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

    struct rt_type *new_type = make_type(RT_KIND_STRUCT, size, &rt_types.types_struct);
    new_type->u._struct.name = name; // TODO: copy?
    new_type->u._struct.field_count = field_count;
    new_type->u._struct.fields = new_fields;
    new_type->desc = type_to_string(new_type);
    return new_type;
}
