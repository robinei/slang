#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
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


typedef intptr_t rt_size_t;


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
};

struct rt_type {
    enum rt_kind kind;

    /* the size of a storage location of this type.
       if 0 then this type is unsized and can't be used for a storage location.
       in that case in needs to be boxed. */
    rt_size_t size;

    /* for RT_KIND_PTR, RT_KIND_ARRAY */
    struct rt_type *target_type;

    /* for RT_KIND_STRUCT */
    u32 field_count;
    struct rt_struct_field *fields;

    /* for RT_KIND_PTR */
    struct rt_type *box_type; /* outermost type if this points to inside a GC-managed box */
    rt_size_t box_offset; /* how many bytes from end of box header to pointee */
};

struct rt_struct_field {
    struct rt_type *type;
    const char *name;
    rt_size_t offset;
};

struct rt_any {
    struct rt_type *type;
    union {
        intptr_t data;
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
    intptr_t header;

    /* the actual data for the boxed value will follow after the box header */
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
};



#define rt_boxheader_get_next(h) ((struct rt_box *)((h) & ~(intptr_t)1))
#define rt_boxheader_set_next(h, next) do { (h) = (intptr_t)(next) | ((h) & (intptr_t)1); } while(0)
#define rt_boxheader_is_marked(h) ((h) & 1)
#define rt_boxheader_set_mark(h) do { (h) |= 1; } while(0)
#define rt_boxheader_clear_mark(h) do { (h) &= ~(intptr_t)1; } while(0)




#define RT_DEF_TYPE(name, kind, size)                                   \
    struct rt_type rt_type_##name = { kind, size, };                    \
    struct rt_type rt_type_ptr_##name = { RT_KIND_PTR, sizeof(void *), &rt_type_##name }; \
    struct rt_type rt_type_boxptr_##name = { RT_KIND_PTR, sizeof(void *), &rt_type_##name, 0, NULL, &rt_type_##name }; \
    struct rt_type rt_type_array_##name = { RT_KIND_ARRAY, 0, &rt_type_##name }; \
    struct rt_type rt_type_boxptr_array_##name = { RT_KIND_PTR, sizeof(void *), &rt_type_array_##name, 0, NULL, &rt_type_array_##name };

#define RT_DEF_SCALAR(name, kind) \
    RT_DEF_TYPE(name, kind, sizeof(name)) \
    struct rt_any rt_new_##name(name value) { \
        struct rt_any any; \
        any.type = &rt_type_##name; \
        any.u.name = value; \
        return any; \
    }

RT_DEF_TYPE(any, RT_KIND_ANY, sizeof(struct rt_any));

RT_DEF_SCALAR(i8, RT_KIND_SIGNED);
RT_DEF_SCALAR(i16, RT_KIND_SIGNED);
RT_DEF_SCALAR(i32, RT_KIND_SIGNED);
RT_DEF_SCALAR(i64, RT_KIND_SIGNED);

RT_DEF_SCALAR(u8, RT_KIND_UNSIGNED);
RT_DEF_SCALAR(u16, RT_KIND_UNSIGNED);
RT_DEF_SCALAR(u32, RT_KIND_UNSIGNED);
RT_DEF_SCALAR(u64, RT_KIND_UNSIGNED);

RT_DEF_SCALAR(f32, RT_KIND_REAL);
RT_DEF_SCALAR(f64, RT_KIND_REAL);

RT_DEF_SCALAR(b8, RT_KIND_BOOL);
RT_DEF_SCALAR(b32, RT_KIND_BOOL);


static struct rt_any rt_any_nil;




static void rt_gc_mark_single(char *ptr, struct rt_type *type);

static void rt_gc_mark_box(struct rt_box *box, struct rt_type *boxed_type) {
    if (!rt_boxheader_is_marked(box->header)) {
        rt_boxheader_set_mark(box->header);
        rt_gc_mark_single((char *)(box + 1), boxed_type);
    }
}

static void rt_gc_mark_struct(char *ptr, struct rt_type *type) {
    u32 field_count = type->field_count;
    struct rt_struct_field *fields = type->fields;
    
    for (u32 i = 0; i < field_count; ++i) {
        struct rt_struct_field *field = fields + i;
        rt_gc_mark_single(ptr + field->offset, field->type);
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

static void rt_gc_mark_array(char *ptr, struct rt_type *type) {
    struct rt_any *any;
    struct rt_type *elem_type = type->target_type;
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
                rt_gc_mark_single((char *)&any->u.data, any->type);
            }
        }
        break;
    case RT_KIND_PTR:
        assert(!elem_type->box_type);
        if (!rt_gc_type_needs_scan(elem_type->target_type)) {
            break;
        }
        for (rt_size_t i = 0; i < length; ++i) {
            rt_gc_mark_single(*(char **)(ptr + i*elem_size), elem_type->target_type);
        }
        break;
    case RT_KIND_STRUCT:
        assert(elem_type->size);
        for (rt_size_t i = 0; i < length; ++i) {
            rt_gc_mark_struct(ptr + i*elem_size, elem_type);
        }
        break;
    case RT_KIND_ARRAY:
        assert(elem_type->size);
        if (!rt_gc_type_needs_scan(elem_type->target_type)) {
            break;
        }
        for (rt_size_t i = 0; i < length; ++i) {
            rt_gc_mark_array(ptr + i*elem_size, elem_type);
        }
        break;
    default:
        break;
    }
}

static void rt_gc_mark_single(char *ptr, struct rt_type *type) {
    struct rt_any *any;
    switch (type->kind) {
    case RT_KIND_ANY:
        any = (struct rt_any *)ptr;
        if (any->type) {
            rt_gc_mark_single((char *)&any->u.data, any->type);
        }
        break;
    case RT_KIND_PTR:
        if (type->box_type) {
            rt_gc_mark_box((struct rt_box *)(*(char **)ptr - type->box_offset - sizeof(struct rt_box)), type->box_type);
        } else {
            rt_gc_mark_single(*(char **)ptr, type->target_type);
        }
        break;
    case RT_KIND_STRUCT:
        rt_gc_mark_struct(ptr, type);
        break;
    case RT_KIND_ARRAY:
        rt_gc_mark_array(ptr, type);
        break;
    default:
        break;
    }
}

void rt_gc_run(struct rt_thread_ctx *ctx) {
    /* mark */
    void **roots = ctx->roots;
    while (roots) {
        struct rt_type **types = roots[1];
        for (u32 i = 0; ; ++i) {
            struct rt_type *type = types[i];
            if (!type) {
                break;
            }
            void *root = roots[i+2];
            rt_gc_mark_single(root, type);
        }
        roots = roots[0];
    }

    /* sweep */
    intptr_t *slot = (intptr_t *)&ctx->boxes;
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
struct rt_struct_field rt_struct_fields_cons[2] = {
    { &rt_type_any, "car", offsetof(struct rt_cons, car) },
    { &rt_type_any, "cdr", offsetof(struct rt_cons, cdr) },
};
struct rt_type rt_type_cons = { RT_KIND_STRUCT, sizeof(struct rt_cons), NULL, 2, rt_struct_fields_cons };
struct rt_type rt_type_boxptr_cons = { RT_KIND_PTR, sizeof(void *), &rt_type_cons, 0, NULL, &rt_type_cons };

struct rt_any rt_new_cons(struct rt_thread_ctx *ctx, struct rt_any car, struct rt_any cdr) {
    struct rt_box *box = rt_alloc_box(ctx, sizeof(struct rt_cons));
    struct rt_cons *cons = (struct rt_cons *)(box + 1);
    cons->car = car;
    cons->cdr = cdr;

    struct rt_any any;
    any.type = &rt_type_boxptr_cons;
    any.u.ptr = box + 1;
    return any;
}

struct rt_any rt_new_array(struct rt_thread_ctx *ctx, rt_size_t length, struct rt_type *ptr_type) {
    assert(ptr_type->kind == RT_KIND_PTR);
    assert(ptr_type->box_type);
    assert(!ptr_type->box_offset);
    
    struct rt_type *array_type = ptr_type->box_type;
    assert(array_type->kind == RT_KIND_ARRAY);
    assert(!array_type->size); /* this boxed array must be unsized (have length stored in box, not type) */
    
    struct rt_type *elem_type = array_type->target_type;
    rt_size_t elem_size = elem_type->size;
    assert(elem_size);
    
    struct rt_box *box = rt_alloc_box(ctx, sizeof(rt_size_t) + length*elem_size);
    *(rt_size_t *)(box + 1) = length;

    struct rt_any any;
    any.type = ptr_type;
    any.u.ptr = box + 1;
    return any;
}



struct rt_array_u8 {
    rt_size_t length;
    u8 data[1];
};

struct rt_string { struct rt_array_u8 chars; };
struct rt_struct_field rt_string_fields[1] = {{ &rt_type_array_u8, "chars", 0 }};
struct rt_type rt_type_string = { RT_KIND_STRUCT, 0, NULL, 1, rt_string_fields };
struct rt_type rt_type_boxptr_string = { RT_KIND_PTR, 0, &rt_type_string, 0, NULL, &rt_type_string };

struct rt_any rt_new_string_from_cstring(struct rt_thread_ctx *ctx, const char *data) {
    rt_size_t length = strlen(data);
    struct rt_box *box = rt_alloc_box(ctx, sizeof(rt_size_t) + length + 1);
    struct rt_string *string = (struct rt_string *)(box + 1);
    string->chars.length = length;
    memcpy(string->chars.data, data, length + 1);

    struct rt_any any;
    any.type = &rt_type_boxptr_string;
    any.u.ptr = box + 1;
    return any;
}


struct rt_symbol { struct rt_string string; };
struct rt_struct_field rt_symbol_fields[1] = {{ &rt_type_string, "string", 0 }};
struct rt_type rt_type_symbol = { RT_KIND_STRUCT, 0, NULL, 1, rt_symbol_fields };
struct rt_type rt_type_boxptr_symbol = { RT_KIND_PTR, 0, &rt_type_symbol, 0, NULL, &rt_type_symbol };





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
        rt_print(*(char **)ptr, type->target_type);
        break;
    case RT_KIND_STRUCT: {
        if (type == &rt_type_string) {
            struct rt_string *string = (struct rt_string *)ptr;
            printf("\"%s\"", string->chars.data);
            break;
        }
        if (type == &rt_type_symbol) {
            struct rt_symbol *sym = (struct rt_symbol *)ptr;
            printf("%s", sym->string.chars.data);
            break;
        }
        if (type == &rt_type_cons) {
            struct rt_cons *cons = (struct rt_cons *)ptr;
            b32 first = TRUE;
            printf("(");
            for (;;) {
                if (!first) {
                    printf(" ");
                }
                first = FALSE;
                rt_print_any(cons->car);
                if (!cons->cdr.type) {
                    break;
                }
                if (cons->cdr.type != &rt_type_boxptr_cons) {
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
        for (u32 i = 0; i < type->field_count; ++i) {
            struct rt_struct_field *f = type->fields + i;
            printf("%s: ", f->name);
            rt_print(ptr + f->offset, f->type);
            if (i != type->field_count - 1) {
                printf(", ");
            }
        }
        printf("}");
        break;
    }
    case RT_KIND_ARRAY: {
        printf("[");
        struct rt_type *elem_type = type->target_type;
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





int main(int argc, char *argv[]) {
    struct rt_thread_ctx ctx = {0,};

    struct rt_any x = rt_new_string_from_cstring(&ctx, "foo");
    struct rt_any y = rt_new_string_from_cstring(&ctx, "bar");
    struct rt_any z = rt_new_string_from_cstring(&ctx, "baz");

    struct rt_any arr = rt_new_array(&ctx, 10, &rt_type_boxptr_array_any);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = z;

    struct rt_any cons = rt_new_cons(&ctx, y, rt_new_cons(&ctx, rt_new_cons(&ctx, rt_new_u8(1), rt_new_u8(2)), rt_new_cons(&ctx, z, rt_any_nil)));
    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = cons;

    rt_box_array_ref(arr.u.ptr, struct rt_any, 2) = rt_new_b32(FALSE);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 3) = rt_new_u8(99);
    rt_box_array_ref(arr.u.ptr, struct rt_any, 4) = rt_new_f64(4.67);

    rt_print_any(arr); printf("\n");

    struct rt_type *types[4] = { &rt_type_any, &rt_type_any, &rt_type_any, NULL };
    void *roots[5] = { ctx.roots, types, &x, &y, &arr };
    ctx.roots = roots;
    rt_gc_run(&ctx);

    printf("-\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 0) = rt_any_nil;
    rt_cdr(cons.u.ptr) = rt_any_nil;
    rt_gc_run(&ctx);

    printf("-\n");

    rt_box_array_ref(arr.u.ptr, struct rt_any, 1) = rt_any_nil;
    rt_gc_run(&ctx);

    printf("-\n");

    ctx.roots = roots[0];
    rt_gc_run(&ctx);
}

