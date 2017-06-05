#ifndef RUNTIME_H
#define RUNTIME_H

#include <stddef.h>
#include <assert.h>

#include "types.h"
#include "hashtable.h"

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

struct rt_sourceloc {
    u32 line;
    u32 col;
};

/* map from cons address to source location */
DECL_HASH_TABLE(rt_sourcemap, struct rt_cons *, struct rt_sourceloc)

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

    struct rt_sourcemap sourcemap;
};

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

/* global type index. TODO: protect with locks if threading becomes a thing */
extern struct rt_type_index rt_types;

/* nil har NULL type and ptr/data. not ideal, but worth it to make 0-inited any values be nil */
extern struct rt_any rt_nil;


struct rt_type *rt_gettype_simple(enum rt_kind kind, rt_size_t size);
struct rt_type *rt_gettype_ptr(struct rt_type *target_type);
struct rt_type *rt_gettype_boxptr(struct rt_type *target_type, struct rt_type *box_type, rt_size_t box_offset);
struct rt_type *rt_gettype_boxed(struct rt_type *target_type);
struct rt_type *rt_gettype_weak(struct rt_type *ptr_type);
struct rt_type *rt_gettype_weak_boxed(struct rt_type *target_type);
struct rt_type *rt_gettype_array(struct rt_type *elem_type, rt_size_t length);
struct rt_type *rt_gettype_boxed_array(struct rt_type *elem_type, rt_size_t length);
struct rt_type *rt_gettype_struct(rt_size_t size, u32 field_count, struct rt_struct_field *fields);

struct rt_any rt_weak_any(struct rt_any any);
b32 rt_any_equals(struct rt_any a, struct rt_any b);

/* allocate a boxed chunk of memory which will be managed by the GC.
   the box header precedes the location pointed to by the returned pointer */
void *rt_gc_alloc(struct rt_thread_ctx *ctx, rt_size_t size);
void rt_gc_run(struct rt_thread_ctx *ctx);

struct rt_any rt_read(struct rt_thread_ctx *ctx, const char *text);

void rt_print(struct rt_any any);


#define RT_DEF_SCALAR(name, kind) \
    static struct rt_any rt_new_##name(name value) { \
        struct rt_any any; \
        any.type = rt_types.name; \
        any.u.name = value; \
        return any; \
    } \
    struct rt_array_##name { \
        rt_size_t length; \
        name data[]; \
    };

RT_DEF_SCALAR(i8, RT_KIND_SIGNED)
RT_DEF_SCALAR(i16, RT_KIND_SIGNED)
RT_DEF_SCALAR(i32, RT_KIND_SIGNED)
RT_DEF_SCALAR(i64, RT_KIND_SIGNED)

RT_DEF_SCALAR(u8, RT_KIND_UNSIGNED)
RT_DEF_SCALAR(u16, RT_KIND_UNSIGNED)
RT_DEF_SCALAR(u32, RT_KIND_UNSIGNED)
RT_DEF_SCALAR(u64, RT_KIND_UNSIGNED)

RT_DEF_SCALAR(f32, RT_KIND_REAL)
RT_DEF_SCALAR(f64, RT_KIND_REAL)

RT_DEF_SCALAR(b8, RT_KIND_BOOL)
RT_DEF_SCALAR(b32, RT_KIND_BOOL)


void rt_init_types();
struct rt_any rt_new_cons(struct rt_thread_ctx *ctx, struct rt_any car, struct rt_any cdr);
struct rt_any rt_new_array(struct rt_thread_ctx *ctx, rt_size_t length, struct rt_type *ptr_type);
struct rt_any rt_new_string(struct rt_thread_ctx *ctx, const char *str);
struct rt_any rt_get_symbol(const char *str);

struct rt_cons {
    struct rt_any car;
    struct rt_any cdr;
};

struct rt_string {
    struct rt_array_u8 chars;
};

struct rt_symbol {
    struct rt_string string;
};

#define rt_box_array_ref(ptr, type, index) (((type *)((char *)(ptr) + sizeof(rt_size_t)))[index])

#define rt_car(ptr) (((struct rt_cons *)(ptr))->car)
#define rt_cdr(ptr) (((struct rt_cons *)(ptr))->cdr)

#endif