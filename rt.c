#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


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

/* boolean */
typedef u8 b8;
typedef u32 b32;

enum { FALSE, TRUE };



enum rt_kind {
    /* gc-managed heap object. contains the boxed value after its header */
    RT_KIND_BOX,
    
    /* reference kinds */
    RT_KIND_PTR,
    RT_KIND_FAT_PTR,
    RT_KIND_BOX_PTR, /* points inside a box, offset by box_offset */
    
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
    u32 size;

    /* for RT_KIND_BOX, RT_KIND_PTR, RT_KIND_BOX_PTR, RT_KIND_ARRAY.
       the "contained" or "pointed to" type */
    struct rt_type *target_type;

    /* for RT_KIND_STRUCT */
    u32 field_count;
    struct rt_struct_field *fields;

    /* for RT_KIND_BOX_PTR */
    struct rt_type *box_type; /* type of enclosing box */
    u32 box_offset; /* how many bytes from start of enclosing box the pointed to value begins */
};

struct rt_struct_field {
    struct rt_type *type;
    const char *name;
    u32 offset;
};

struct rt_fatptr {
    void *ptr;
    struct rt_type *type;
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
    struct rt_type rt_type_box_##name = { RT_KIND_BOX, 0, &rt_type_##name }; \
    struct rt_type rt_type_ptr_##name = { RT_KIND_PTR, sizeof(void *), &rt_type_##name }; \
    struct rt_type rt_type_array_##name = { RT_KIND_ARRAY, 0, &rt_type_##name }; \
    struct rt_type rt_type_box_array_##name = { RT_KIND_BOX, 0, &rt_type_array_##name };

#define RT_DEF_SCALAR(name, kind) RT_DEF_TYPE(name, kind, sizeof(name))

RT_DEF_TYPE(fatptr, RT_KIND_FAT_PTR, sizeof(struct rt_fatptr));

RT_DEF_SCALAR(b8, RT_KIND_BOOL);
RT_DEF_SCALAR(b32, RT_KIND_BOOL);

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


struct rt_box rt_true = {0};
struct rt_box rt_false = {0};




static void rt_gc_mark_single(char *ptr, struct rt_type *type);

static void rt_gc_mark_box(struct rt_box *box, struct rt_type *boxed_type) {
    if (!box || rt_boxheader_is_marked(box->header)) {
        return;
    }
    rt_boxheader_set_mark(box->header);
    rt_gc_mark_single((char *)box + sizeof(struct rt_box), boxed_type);
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
    }
    return TRUE;
}

static void rt_gc_mark_array(char *ptr, struct rt_type *type) {
    struct rt_fatptr *f;
    struct rt_type *elem_type = type->target_type;
    u32 elem_size = elem_type->size;
    
    u32 length;
    if (type->size) {
        length = type->size / elem_size;
    } else {
        /* an unsized array is a boxed array, which starts with a 32-bit length */
        length = *(u32 *)ptr;
        ptr += sizeof(u32);
    }
    
    switch (elem_type->kind) {
    case RT_KIND_PTR:
        if (!rt_gc_type_needs_scan(elem_type->target_type)) {
            break;
        }
        for (u32 i = 0; i < length; ++i) {
            rt_gc_mark_single(*(char **)(ptr + i*elem_size), elem_type->target_type);
        }
        break;
    case RT_KIND_FAT_PTR:
        for (u32 i = 0; i < length; ++i) {
            f = (struct rt_fatptr *)(ptr + i*elem_size);
            if (f->ptr) {
                rt_gc_mark_single(f->ptr, f->type);
            }
        }
        break;
    case RT_KIND_STRUCT:
        assert(elem_type->size);
        for (u32 i = 0; i < length; ++i) {
            rt_gc_mark_struct(ptr + i*elem_size, elem_type);
        }
        break;
    case RT_KIND_ARRAY:
        assert(elem_type->size);
        if (!rt_gc_type_needs_scan(elem_type->target_type)) {
            break;
        }
        for (u32 i = 0; i < length; ++i) {
            rt_gc_mark_array(ptr + i*elem_size, elem_type);
        }
        break;
    }
}

static void rt_gc_mark_single(char *ptr, struct rt_type *type) {
    struct rt_fatptr *f;
    switch (type->kind) {
    case RT_KIND_BOX:
        rt_gc_mark_box((struct rt_box *)ptr, type->target_type);
        break;
    case RT_KIND_PTR:
        rt_gc_mark_single(*(char **)ptr, type->target_type);
        break;
    case RT_KIND_FAT_PTR:
        f = (struct rt_fatptr *)ptr;
        if (f->ptr) {
            rt_gc_mark_single(f->ptr, f->type);
        }
        break;
    case RT_KIND_BOX_PTR:
        rt_gc_mark_box((struct rt_box *)(ptr - type->box_offset), type->box_type);
        break;
    case RT_KIND_STRUCT:
        rt_gc_mark_struct(ptr, type);
        break;
    case RT_KIND_ARRAY:
        rt_gc_mark_array(ptr, type);
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

struct rt_box *rt_alloc_box(struct rt_thread_ctx *ctx, u32 size) {
    struct rt_box *box = (struct rt_box *)calloc(1, sizeof(struct rt_box) + size);
    rt_boxheader_set_next(box->header, ctx->boxes);
    ctx->boxes = box;
    return box;
}





struct rt_cons {
    struct rt_fatptr car;
    struct rt_fatptr cdr;
};

struct rt_struct_field rt_struct_fields_cons[2] = {
    { &rt_type_fatptr, "car", offsetof(struct rt_cons, car) },
    { &rt_type_fatptr, "cdr", offsetof(struct rt_cons, cdr) },
};

struct rt_type rt_type_cons = { RT_KIND_STRUCT, sizeof(struct rt_cons), NULL, 2, rt_struct_fields_cons };
struct rt_type rt_type_box_cons = { RT_KIND_BOX, 0, &rt_type_cons };

struct rt_box *rt_new_cons(struct rt_thread_ctx *ctx, struct rt_fatptr car, struct rt_fatptr cdr) {
    struct rt_box *box = rt_alloc_box(ctx, sizeof(struct rt_cons));
    struct rt_cons *cons = (struct rt_cons *)((char *)box + sizeof(struct rt_box));
    cons->car = car;
    cons->cdr = cdr;
    return box;
}

struct rt_box *rt_new_array(struct rt_thread_ctx *ctx, u32 length, struct rt_type *type) {
    assert(type->kind == RT_KIND_ARRAY);
    assert(!type->size); /* boxed arrays are unsized (have length stored in box, not type) */
    u32 elem_size = type->target_type->size;
    assert(elem_size);
    struct rt_box *box = rt_alloc_box(ctx, sizeof(u32) + length*elem_size);
    *(u32 *)((char *)box + sizeof(struct rt_box)) = length;
    return box;
}

struct rt_box *rt_new_b32(b32 value) {
    return value ? &rt_true : &rt_false;
}

struct rt_box *rt_new_u32(struct rt_thread_ctx *ctx, u32 value) {
    struct rt_box *box = rt_alloc_box(ctx, sizeof(u32));
    *(u32 *)((char *)box + sizeof(struct rt_box)) = value;
    return box;
}

struct rt_array_u8 {
    u32 length;
    u8 data[1];
};

struct rt_string { struct rt_array_u8 chars; };
struct rt_struct_field rt_string_fields[1] = {{ &rt_type_array_u8, "chars", 0 }};
struct rt_type rt_type_string = { RT_KIND_STRUCT, 0, NULL, 1, rt_string_fields };

struct rt_box *rt_new_string_from_cstring(struct rt_thread_ctx *ctx, const char *data) {
    u32 length = strlen(data);
    struct rt_box *box = rt_alloc_box(ctx, sizeof(u32) + length + 1);
    struct rt_string *string = (struct rt_string *)(box + 1);
    string->chars.length = length;
    memcpy(string->chars.data, data, length + 1);
    return box;
}


struct rt_symbol { struct rt_string string; };
struct rt_struct_field rt_symbol_fields[1] = {{ &rt_type_string, "string", 0 }};
struct rt_type rt_type_symbol = { RT_KIND_STRUCT, 0, NULL, 1, rt_symbol_fields };



static struct rt_fatptr rt_make_fatptr(void *ptr, struct rt_type *type) {
    struct rt_fatptr f = { ptr, type };
    return f;
}

static struct rt_fatptr rt_fatptr_nil = { NULL, NULL };


#define rt_box_array_ref(box, type, index) (((type *)((char *)(box) + sizeof(struct rt_box) + sizeof(u32)))[index])

#define rt_car(box) (((struct rt_cons *)((char *)(box) + sizeof(struct rt_box)))->car)
#define rt_cdr(box) (((struct rt_cons *)((char *)(box) + sizeof(struct rt_box)))->cdr)


int main(int argc, char *argv[]) {
    struct rt_thread_ctx ctx = {0,};

    struct rt_fatptr x = rt_make_fatptr(rt_new_u32(&ctx, 123), &rt_type_box_u32);
    struct rt_fatptr y = rt_make_fatptr(rt_new_u32(&ctx, 100), &rt_type_box_u32);
    struct rt_fatptr z = rt_make_fatptr(rt_new_u32(&ctx, 33), &rt_type_box_u32);

    struct rt_fatptr arr = rt_make_fatptr(rt_new_array(&ctx, 10, &rt_type_array_fatptr), &rt_type_box_array_fatptr);
    rt_box_array_ref(arr.ptr, struct rt_fatptr, 0) = z;

    struct rt_fatptr cons = rt_make_fatptr(rt_new_cons(&ctx, y, z), &rt_type_box_cons);
    rt_box_array_ref(arr.ptr, struct rt_fatptr, 1) = cons;

    struct rt_type *types[4] = { &rt_type_fatptr, &rt_type_fatptr, &rt_type_fatptr, NULL };
    void *roots[5] = { ctx.roots, types, &x, &y, &arr };
    ctx.roots = roots;
    rt_gc_run(&ctx);

    printf("-\n");

    rt_box_array_ref(arr.ptr, struct rt_fatptr, 0) = rt_fatptr_nil;
    rt_cdr(cons.ptr) = rt_fatptr_nil;
    rt_gc_run(&ctx);

    printf("-\n");

    rt_box_array_ref(arr.ptr, struct rt_fatptr, 1) = rt_fatptr_nil;
    rt_gc_run(&ctx);

    printf("-\n");

    ctx.roots = roots[0];
    rt_gc_run(&ctx);
}

