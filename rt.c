#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "hashtable.h"
#include "murmur3.h"


typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

enum { FALSE, TRUE };

/* boolean */
typedef u8 b8;
typedef u32 b32;


typedef uintptr_t rt_size_t;


enum rt_kind {
    /* tuple of type pointer and value, where value is described by type */
    RT_KIND_ANY,
    
    /* pointer to inside boxes or to stack or unmanaged memory */
    RT_KIND_PTR,
    
    /* composite kinds */
    RT_KIND_STRUCT,
    RT_KIND_ARRAY,

    /* scalar kinds */
    RT_KIND_BOOL,
    RT_KIND_SIGNED,
    RT_KIND_UNSIGNED,
    RT_KIND_REAL,

    /* function */
    RT_KIND_FUN,
};

enum {
    RT_TYPE_FLAG_WEAK_PTR = 1 << 0,
};

struct rt_type {
    enum rt_kind kind;
    u32 flags;

    /* the size of a storage location of this type.
       if 0 then this type is unsized and can't be used for a storage location.
       in that case in needs to be boxed. */
    rt_size_t size;

    /* for chaining types in type tables */
    struct rt_type *next;

    union {
        struct {
            struct rt_type *target_type;
            /* outermost type if this points to inside a GC-managed box */
            struct rt_type *box_type;
            /* how many bytes from end of box header to pointee */
            rt_size_t box_offset;
        } ptr;
        struct {
            u32 field_count;
            struct rt_struct_field *fields;
        } _struct;
        struct {
            struct rt_type *elem_type;
        } array;
        struct {
            u32 param_count;
            struct rt_fun_param *params;
            struct rt_type *return_type;
        } fun;
    } u;
};

struct rt_struct_field {
    struct rt_type *type;
    const char *name;
    rt_size_t offset;
};

struct rt_fun_param {
    struct rt_type *type;
    const char *name;
};

struct rt_any {
    struct rt_type *type;
    union {
        uintptr_t data;
        void *ptr;
        u8 u8;
        u16 u16;
        u32 u32;
        u64 u64;
        i8 i8;
        i16 i16;
        i32 i32;
        i64 i64;
        f32 f32;
        f64 f64;
        b8 b8;
        b32 b32;
    } u;
};

struct rt_box {
    /* pointer used to chain all allocated boxes so the GC can run a sweep.
       bit 0 is the GC mark bit, so the box pointers must be 2-byte aligned */
    uintptr_t header;

    /* the actual data for the boxed value will follow after the box header */
};

struct rt_weakptr_entry {
    void **ptr;
    struct rt_type *type;
};

struct rt_thread_ctx {
    /* array of pointers to active roots. used for GC mark phase.
       the first element is actually a pointer to another root array, so this
       forms a linked list of root arrays, one per stack frame.
       the second element points to a NULL-terminated array of type pointers,
       one for each remaining element of the roots array.
       the number of elements in the type array determines how many elements
       from the roots array will be scanned */
    void **roots;

    /* linked list of all allocated boxes. used for GC sweep phase. */
    struct rt_box *boxes;

    /* for keeping track of weak pointers encountered during GC mark phase */
    u32 num_weakptrs;
    u32 max_weakptrs;
    struct rt_weakptr_entry *weakptrs;
};




#define rt_boxheader_get_next(h) ((struct rt_box *)((h) & ~(uintptr_t)1))
#define rt_boxheader_set_next(h, next) do { (h) = (uintptr_t)(next) | ((h) & (uintptr_t)1); } while(0)
#define rt_boxheader_is_marked(h) ((h) & 1)
#define rt_boxheader_set_mark(h) do { (h) |= 1; } while(0)
#define rt_boxheader_clear_mark(h) do { (h) &= ~(uintptr_t)1; } while(0)




struct rt_type_index {
    /* for lookup and "uniquification" */
    struct rt_type *types_simple;
    struct rt_type *types_ptr;
    struct rt_type *types_boxptr;
    struct rt_type *types_weakptr;
    struct rt_type *types_array;
    struct rt_type *types_struct;

    /* type shorthands */

    struct rt_type *any;
    
    struct rt_type *u8;
    struct rt_type *u16;
    struct rt_type *u32;
    struct rt_type *u64;

    struct rt_type *i8;
    struct rt_type *i16;
    struct rt_type *i32;
    struct rt_type *i64;

    struct rt_type *f32;
    struct rt_type *f64;

    struct rt_type *b8;
    struct rt_type *b32;

    struct rt_type *cons;
    struct rt_type *boxed_cons;
    
    struct rt_type *string;
    struct rt_type *boxed_string;
    
    struct rt_type *symbol;
    struct rt_type *ptr_symbol;
};

/* global type index. protect with locks if threading becomes a thing */
static struct rt_type_index rt_types;


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

#define RT_DEF_SCALAR_MAKER(name, kind) \
    struct rt_any rt_new_##name(name value) { \
        struct rt_any any; \
        any.type = rt_types.name; \
        any.u.name = value; \
        return any; \
    }

RT_DEF_SCALAR_MAKER(i8, RT_KIND_SIGNED);
RT_DEF_SCALAR_MAKER(i16, RT_KIND_SIGNED);
RT_DEF_SCALAR_MAKER(i32, RT_KIND_SIGNED);
RT_DEF_SCALAR_MAKER(i64, RT_KIND_SIGNED);

RT_DEF_SCALAR_MAKER(u8, RT_KIND_UNSIGNED);
RT_DEF_SCALAR_MAKER(u16, RT_KIND_UNSIGNED);
RT_DEF_SCALAR_MAKER(u32, RT_KIND_UNSIGNED);
RT_DEF_SCALAR_MAKER(u64, RT_KIND_UNSIGNED);

RT_DEF_SCALAR_MAKER(f32, RT_KIND_REAL);
RT_DEF_SCALAR_MAKER(f64, RT_KIND_REAL);

RT_DEF_SCALAR_MAKER(b8, RT_KIND_BOOL);
RT_DEF_SCALAR_MAKER(b32, RT_KIND_BOOL);

struct rt_any rt_nil;




static void rt_gc_mark_single(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type);

static void rt_gc_mark_box(struct rt_thread_ctx *ctx, struct rt_box *box, struct rt_type *boxed_type) {
    if (!rt_boxheader_is_marked(box->header)) {
        rt_boxheader_set_mark(box->header);
        rt_gc_mark_single(ctx, (char *)(box + 1), boxed_type);
    }
}

static void rt_gc_mark_struct(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type) {
    u32 field_count = type->u._struct.field_count;
    struct rt_struct_field *fields = type->u._struct.fields;
    
    for (u32 i = 0; i < field_count; ++i) {
        struct rt_struct_field *field = fields + i;
        rt_gc_mark_single(ctx, ptr + field->offset, field->type);
    }
}

static b32 rt_gc_type_needs_scan(struct rt_type *type) {
    switch (type->kind) {
    case RT_KIND_BOOL:
    case RT_KIND_SIGNED:
    case RT_KIND_UNSIGNED:
    case RT_KIND_REAL:
        return FALSE;
    default:
        return TRUE;
    }
}

static void rt_gc_mark_array(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type) {
    struct rt_any *any;
    struct rt_type *elem_type = type->u.array.elem_type;
    rt_size_t elem_size = elem_type->size;
    
    rt_size_t length;
    if (type->size) {
        length = type->size / elem_size;
    } else {
        /* an unsized array is a boxed array, which starts with a 32-bit length */
        length = *(rt_size_t *)ptr;
        ptr += sizeof(rt_size_t);
    }
    
    switch (elem_type->kind) {
    case RT_KIND_ANY:
        for (rt_size_t i = 0; i < length; ++i) {
            any = (struct rt_any *)(ptr + i*elem_size);
            if (any->type) {
                rt_gc_mark_single(ctx, (char *)&any->u.data, any->type);
            }
        }
        break;
    case RT_KIND_PTR:
        assert(!elem_type->u.ptr.box_type);
        if (!rt_gc_type_needs_scan(elem_type->u.ptr.target_type)) {
            break;
        }
        for (rt_size_t i = 0; i < length; ++i) {
            rt_gc_mark_single(ctx, *(char **)(ptr + i*elem_size), elem_type->u.ptr.target_type);
        }
        break;
    case RT_KIND_STRUCT:
        assert(elem_type->size);
        for (rt_size_t i = 0; i < length; ++i) {
            rt_gc_mark_struct(ctx, ptr + i*elem_size, elem_type);
        }
        break;
    case RT_KIND_ARRAY:
        assert(elem_type->size);
        if (!rt_gc_type_needs_scan(elem_type->u.array.elem_type)) {
            break;
        }
        for (rt_size_t i = 0; i < length; ++i) {
            rt_gc_mark_array(ctx, ptr + i*elem_size, elem_type);
        }
        break;
    default:
        break;
    }
}

static void rt_gc_add_weakptr(struct rt_thread_ctx *ctx, void **ptr, struct rt_type *type) {
    if (ctx->num_weakptrs == ctx->max_weakptrs) {
        ctx->max_weakptrs = ctx->max_weakptrs ? ctx->max_weakptrs * 2 : 16;
        ctx->weakptrs = realloc(ctx->weakptrs, sizeof(struct rt_weakptr_entry) * ctx->max_weakptrs);
    }
    struct rt_weakptr_entry *e = &ctx->weakptrs[ctx->num_weakptrs++];
    e->ptr = ptr;
    e->type = type;
}

static void rt_gc_mark_single(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type) {
    struct rt_any *any;
    switch (type->kind) {
    case RT_KIND_ANY:
        any = (struct rt_any *)ptr;
        if (any->type) {
            rt_gc_mark_single(ctx, (char *)&any->u.data, any->type);
        }
        break;
    case RT_KIND_PTR:
        if (!*(char **)ptr) {
            break;
        }
        if (type->u.ptr.box_type) {
            if (type->flags & RT_TYPE_FLAG_WEAK_PTR) {
                rt_gc_add_weakptr(ctx, (void **)ptr, type);
            } else {
                rt_gc_mark_box(ctx,
                    (struct rt_box *)(*(char **)ptr - type->u.ptr.box_offset - sizeof(struct rt_box)),
                    type->u.ptr.box_type);
            }
        } else {
            rt_gc_mark_single(ctx, *(char **)ptr, type->u.ptr.target_type);
        }
        break;
    case RT_KIND_STRUCT:
        rt_gc_mark_struct(ctx, ptr, type);
        break;
    case RT_KIND_ARRAY:
        rt_gc_mark_array(ctx, ptr, type);
        break;
    default:
        break;
    }
}

void rt_gc_run(struct rt_thread_ctx *ctx) {
    /* mark */
    ctx->num_weakptrs = 0;
    void **roots = ctx->roots;
    while (roots) {
        struct rt_type **types = roots[1];
        for (u32 i = 0; ; ++i) {
            struct rt_type *type = types[i];
            if (!type) {
                break;
            }
            void *root = roots[i+2];
            rt_gc_mark_single(ctx, root, type);
        }
        roots = roots[0];
    }

    /* null out the weak pointers */
    for (u32 i = 0; i < ctx->num_weakptrs; ++i) {
        struct rt_weakptr_entry e = ctx->weakptrs[i];
        struct rt_box *box = (struct rt_box *)(*(char **)e.ptr - e.type->u.ptr.box_offset - sizeof(struct rt_box));
        if (!rt_boxheader_is_marked(box->header)) {
            *e.ptr = NULL;
        }
    }

    /* sweep */
    uintptr_t *slot = (uintptr_t *)&ctx->boxes;
    struct rt_box *unreachable = NULL;
    while (1) {
        struct rt_box *box = rt_boxheader_get_next(*slot);
        if (!box) {
            break;
        }
        if (rt_boxheader_is_marked(box->header)) {
            rt_boxheader_clear_mark(box->header);
            slot = &box->header;
        } else {
            rt_boxheader_set_next(*slot, rt_boxheader_get_next(box->header));
            rt_boxheader_set_next(box->header, unreachable);
            unreachable = box;
        }
    }

    /* free unreachable boxes. could be done on another thread */
    while (unreachable) {
        struct rt_box *box = unreachable;
        unreachable = rt_boxheader_get_next(box->header);
        printf("freeing 1 object\n");
        free(box);
    }
}

struct rt_box *rt_alloc_box(struct rt_thread_ctx *ctx, rt_size_t size) {
    struct rt_box *box = (struct rt_box *)calloc(1, sizeof(struct rt_box) + size);
    rt_boxheader_set_next(box->header, ctx->boxes);
    ctx->boxes = box;
    return box;
}





struct rt_cons {
    struct rt_any car;
    struct rt_any cdr;
};

struct rt_array_u8 {
    rt_size_t length;
    u8 data[1];
};

struct rt_string { struct rt_array_u8 chars; };

struct rt_symbol { struct rt_string string; };

struct rt_any rt_new_cons(struct rt_thread_ctx *ctx, struct rt_any car, struct rt_any cdr) {
    struct rt_box *box = rt_alloc_box(ctx, sizeof(struct rt_cons));
    struct rt_cons *cons = (struct rt_cons *)(box + 1);
    cons->car = car;
    cons->cdr = cdr;

    struct rt_any any;
    any.type = rt_types.boxed_cons;
    any.u.ptr = box + 1;
    return any;
}

struct rt_any rt_new_array(struct rt_thread_ctx *ctx, rt_size_t length, struct rt_type *ptr_type) {
    assert(ptr_type->kind == RT_KIND_PTR);
    assert(ptr_type->u.ptr.box_type);
    assert(!ptr_type->u.ptr.box_offset);
    
    struct rt_type *array_type = ptr_type->u.ptr.box_type;
    assert(array_type->kind == RT_KIND_ARRAY);
    assert(!array_type->size); /* this boxed array must be unsized (have length stored in box, not type) */
    
    struct rt_type *elem_type = array_type->u.array.elem_type;
    rt_size_t elem_size = elem_type->size;
    assert(elem_size);
    
    struct rt_box *box = rt_alloc_box(ctx, sizeof(rt_size_t) + length*elem_size);
    *(rt_size_t *)(box + 1) = length;

    struct rt_any any;
    any.type = ptr_type;
    any.u.ptr = box + 1;
    return any;
}

struct rt_any rt_new_string_from_cstring(struct rt_thread_ctx *ctx, const char *data) {
    rt_size_t length = strlen(data);
    struct rt_box *box = rt_alloc_box(ctx, sizeof(rt_size_t) + length + 1);
    struct rt_string *string = (struct rt_string *)(box + 1);
    string->chars.length = length;
    memcpy(string->chars.data, data, length + 1);

    struct rt_any any;
    any.type = rt_types.boxed_string;
    any.u.ptr = box + 1;
    return any;
}




#define rt_box_array_ref(box, type, index) (((type *)((char *)(box) + sizeof(rt_size_t)))[index])

#define rt_car(box) (((struct rt_cons *)(box))->car)
#define rt_cdr(box) (((struct rt_cons *)(box))->cdr)



void rt_print_any(struct rt_any any);

void rt_print(char *ptr, struct rt_type *type) {
    if (!type) {
        printf("nil");
        return;
    }
    switch (type->kind) {
    case RT_KIND_ANY: {
        struct rt_any *any = (struct rt_any *)ptr;
        rt_print((char *)&any->u.data, any->type);
        break;
    }
    case RT_KIND_PTR:
        rt_print(*(char **)ptr, type->u.ptr.target_type);
        break;
    case RT_KIND_STRUCT: {
        if (type == rt_types.string) {
            struct rt_string *string = (struct rt_string *)ptr;
            printf("\"%s\"", string->chars.data);
            break;
        }
        if (type == rt_types.symbol) {
            struct rt_symbol *sym = (struct rt_symbol *)ptr;
            printf("%s", sym->string.chars.data);
            break;
        }
        if (type == rt_types.cons) {
            struct rt_cons *cons = (struct rt_cons *)ptr;
            if (!cons) {
                printf("nil");
                break;
            }
            b32 first = TRUE;
            printf("(");
            while (cons) {
                if (!first) {
                    printf(" ");
                }
                first = FALSE;
                rt_print_any(cons->car);
                if (!cons->cdr.type) {
                    break;
                }
                if (cons->cdr.type != rt_types.boxed_cons) {
                    printf(" . ");
                    rt_print_any(cons->cdr);
                    break;
                }
                cons = (struct rt_cons *)cons->cdr.u.ptr;
            }
            printf(")");
            break;
        }
        printf("{");
        u32 field_count = type->u._struct.field_count;
        for (u32 i = 0; i < field_count; ++i) {
            struct rt_struct_field *f = type->u._struct.fields + i;
            printf("%s: ", f->name);
            rt_print(ptr + f->offset, f->type);
            if (i != field_count - 1) {
                printf(", ");
            }
        }
        printf("}");
        break;
    }
    case RT_KIND_ARRAY: {
        printf("[");
        struct rt_type *elem_type = type->u.array.elem_type;
        rt_size_t elem_size = elem_type->size;
        rt_size_t length;
        if (type->size) {
            length = type->size / elem_size;
        } else {
            length = *(rt_size_t *)ptr;
            ptr += sizeof(rt_size_t);
        }
        for (rt_size_t i = 0; i < length; ++i) {
            rt_print(ptr + i*elem_size, elem_type);
            if (i != length - 1) {
                printf(" ");
            }
        }
        printf("]");
        break;
    }
    case RT_KIND_BOOL:
        switch (type->size) {
        case 1: printf("%s", *(b8 *)ptr ? "true" : "false"); break;
        case 4: printf("%s", *(b32 *)ptr ? "true" : "false"); break;
        }
        break;
    case RT_KIND_SIGNED:
        switch (type->size) {
        case 1: printf("%d", *(i8 *)ptr); break;
        case 2: printf("%d", *(i16 *)ptr); break;
        case 4: printf("%d", *(i32 *)ptr); break;
        case 8: printf(PRId64, *(i64 *)ptr); break;
        }
        break;
    case RT_KIND_UNSIGNED:
        switch (type->size) {
        case 1: printf("%u", *(u8 *)ptr); break;
        case 2: printf("%u", *(u16 *)ptr); break;
        case 4: printf("%u", *(u32 *)ptr); break;
        case 8: printf(PRIu64, *(u64 *)ptr); break;
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

void rt_print_any(struct rt_any any) {
    rt_print((char *)&any.u.data, any.type);
}


void rt_init_types() {
    rt_types.any = rt_gettype_simple(RT_KIND_ANY, sizeof(struct rt_any));

    rt_types.u8 = rt_gettype_simple(RT_KIND_UNSIGNED, sizeof(u8));
    rt_types.u16 = rt_gettype_simple(RT_KIND_UNSIGNED, sizeof(u16));
    rt_types.u32 = rt_gettype_simple(RT_KIND_UNSIGNED, sizeof(u32));
    rt_types.u64 = rt_gettype_simple(RT_KIND_UNSIGNED, sizeof(u64));

    rt_types.i8 = rt_gettype_simple(RT_KIND_SIGNED, sizeof(i8));
    rt_types.i16 = rt_gettype_simple(RT_KIND_SIGNED, sizeof(i16));
    rt_types.i32 = rt_gettype_simple(RT_KIND_SIGNED, sizeof(i32));
    rt_types.i64 = rt_gettype_simple(RT_KIND_SIGNED, sizeof(i64));

    rt_types.f32 = rt_gettype_simple(RT_KIND_REAL, sizeof(f32));
    rt_types.f64 = rt_gettype_simple(RT_KIND_REAL, sizeof(u64));

    rt_types.b8 = rt_gettype_simple(RT_KIND_BOOL, sizeof(b8));
    rt_types.b32 = rt_gettype_simple(RT_KIND_BOOL, sizeof(b32));

    struct rt_struct_field cons_fields[2] = {
        { rt_types.any, "car", offsetof(struct rt_cons, car) },
        { rt_types.any, "cdr", offsetof(struct rt_cons, cdr) },
    };
    rt_types.cons = rt_gettype_struct(sizeof(struct rt_cons), 2, cons_fields);
    rt_types.boxed_cons = rt_gettype_boxed(rt_types.cons);

    struct rt_struct_field string_fields[1] = {{ rt_gettype_array(rt_types.u8, 0), "chars", 0 }};
    rt_types.string = rt_gettype_struct(0, 1, string_fields);
    rt_types.boxed_string = rt_gettype_boxed(rt_types.string);

    struct rt_struct_field symbol_fields[1] = {{ rt_types.string, "string", 0 }};
    rt_types.symbol = rt_gettype_struct(0, 1, symbol_fields);
    rt_types.ptr_symbol = rt_gettype_ptr(rt_types.symbol);
}


struct rt_any rt_weak_any(struct rt_any any) {
    if (!any.type) {
        return any;
    }
    if (any.type->kind != RT_KIND_PTR) {
        return any;
    }
    struct rt_any new_any;
    new_any.type = rt_gettype_weakptr(any.type);
    new_any.u.ptr = any.u.ptr;
    return new_any;
}






int main(int argc, char *argv[]) {
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

