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

    RT_KIND_NIL,
    
    /* pointer to inside boxes or to stack or unmanaged memory */
    RT_KIND_PTR,
    
    /* composite kinds */
    RT_KIND_STRUCT,
    RT_KIND_ARRAY,
    RT_KIND_CONS,

    /* scalar kinds */
    RT_KIND_BOOL,
    RT_KIND_SIGNED,
    RT_KIND_UNSIGNED,
    RT_KIND_REAL,

    /* function */
    RT_KIND_FUNC,

    RT_KIND_TYPE,
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
        } func;
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
    /* non't use directly as it can be NULL which should mean the type is rt_types.nil */
    struct rt_type *_type;

    union {
        uintptr_t data;
        
        void *ptr;
        struct rt_cons *cons;
        struct rt_string *string;
        struct rt_symbol *symbol;
        
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
        
        bool _bool;
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

struct rt_symbol_index {
    struct rt_any any;
    struct rt_any nil;

    struct rt_any u8;
    struct rt_any u16;
    struct rt_any u32;
    struct rt_any u64;

    struct rt_any i8;
    struct rt_any i16;
    struct rt_any i32;
    struct rt_any i64;

    struct rt_any f32;
    struct rt_any f64;

    struct rt_any _bool;

    struct rt_any cons;
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
    struct rt_type *nil;
    
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

    struct rt_type *_bool;

    struct rt_type *cons;
    struct rt_type *boxed_cons;
    
    struct rt_type *string;
    struct rt_type *boxed_string;
    
    struct rt_type *symbol;
    struct rt_type *ptr_symbol;
};

/* global value indexes . TODO: protect with locks if threading becomes a thing */
extern struct rt_symbol_index rt_symbols;
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

bool rt_any_to_bool(struct rt_any a);
f64 rt_any_to_f64(struct rt_any a);
u64 rt_any_to_u64(struct rt_any a);
i64 rt_any_to_i64(struct rt_any a);
struct rt_any rt_weak_any(struct rt_any any);
struct rt_any rt_any_to_signed(struct rt_any a);
struct rt_any rt_any_to_unsigned(struct rt_any a);
bool rt_any_equals(struct rt_any a, struct rt_any b);

#define rt_any_get_type(any) ((any)._type ? (any)._type : rt_types.nil)
#define rt_any_is_nil(any) (!(any)._type || (any)._type == rt_types.nil)
#define rt_any_is_bool(any) ((any)._type && (any)._type->kind == RT_KIND_BOOL)
#define rt_any_is_unsigned(any) ((any)._type && (any)._type->kind == RT_KIND_UNSIGNED)
#define rt_any_is_signed(any) ((any)._type && (any)._type->kind == RT_KIND_SIGNED)
#define rt_any_is_real(any) ((any)._type && (any)._type->kind == RT_KIND_REAL)
#define rt_any_is_func(any) ((any)._type && (any)._type->kind == RT_KIND_FUNC)
#define rt_any_is_cons(any) ((any)._type && (any)._type->kind == RT_KIND_CONS)
#define rt_any_is_symbol(any) (rt_any_get_type(any) == rt_types.ptr_symbol)

#define rt_any_from_ptr(type, pointer) ((struct rt_any) { type, { .ptr = (pointer) } })
#define rt_any_from_cons(Cons) ((struct rt_any) { rt_types.boxed_cons, { .cons = (Cons) } })
#define rt_any_from_string(str) ((struct rt_any) { rt_types.boxed_string, { .string = (str) } })
#define rt_any_from_symbol(sym) ((struct rt_any) { rt_types.ptr_symbol, { .symbol = (sym) } })

/* allocate a boxed chunk of memory which will be managed by the GC.
   the box header precedes the location pointed to by the returned pointer */
void *rt_gc_alloc(struct rt_thread_ctx *ctx, rt_size_t size);
void rt_gc_run(struct rt_thread_ctx *ctx);

struct rt_any rt_read(struct rt_thread_ctx *ctx, const char *text);

void rt_print(struct rt_any any);

struct rt_type *rt_parse_type(struct rt_thread_ctx *ctx, struct rt_any parent_form, struct rt_any form);


#define RT_DEF_SCALAR_FULL(type, name, propername, kind) \
    static struct rt_any rt_new_##propername(type value) { \
        struct rt_any any; \
        any._type = rt_types.name; \
        any.u.name = value; \
        return any; \
    } \
    struct rt_array_##propername { \
        rt_size_t length; \
        type data[]; \
    };

#define RT_DEF_SCALAR(typ, kind) \
    RT_DEF_SCALAR_FULL(typ, typ, typ, kind)

RT_DEF_SCALAR(u8, RT_KIND_UNSIGNED)
RT_DEF_SCALAR(u16, RT_KIND_UNSIGNED)
RT_DEF_SCALAR(u32, RT_KIND_UNSIGNED)
RT_DEF_SCALAR(u64, RT_KIND_UNSIGNED)

RT_DEF_SCALAR(i8, RT_KIND_SIGNED)
RT_DEF_SCALAR(i16, RT_KIND_SIGNED)
RT_DEF_SCALAR(i32, RT_KIND_SIGNED)
RT_DEF_SCALAR(i64, RT_KIND_SIGNED)

RT_DEF_SCALAR(f32, RT_KIND_REAL)
RT_DEF_SCALAR(f64, RT_KIND_REAL)

RT_DEF_SCALAR_FULL(bool, _bool, bool, RT_KIND_BOOL)


void rt_init_types();
struct rt_type *rt_lookup_simple_type(struct rt_any sym);
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

struct rt_func {
    struct rt_astnode *ast;
};

#define rt_box_array_ref(ptr, type, index) (((type *)((char *)(ptr) + sizeof(rt_size_t)))[index])

#define rt_car(ptr) (((struct rt_cons *)(ptr))->car)
#define rt_cdr(ptr) (((struct rt_cons *)(ptr))->cdr)





enum rt_astnode_type {
    RT_ASTNODE_FUNC,
    RT_ASTNODE_SCOPE,
    RT_ASTNODE_BLOCK,
    RT_ASTNODE_LITERAL,
    RT_ASTNODE_GET_LOCAL,
    RT_ASTNODE_SET_LOCAL,
    RT_ASTNODE_GET_CONST_GLOBAL,
    RT_ASTNODE_DEF_CONST_GLOBAL,
    RT_ASTNODE_COND,
    RT_ASTNODE_LOOP,
    RT_ASTNODE_CALL,
};

struct rt_scope_slot {
    struct rt_type *type;
    const char *name;
};

struct rt_astnode {
    enum rt_astnode_type node_type;
    struct rt_type *result_type;
    struct rt_astnode *parent_scope;
    struct rt_sourceloc sourceloc;

    union {
        struct {
            const char *name;
            struct rt_astnode *scope_expr;
        } func;

        struct {
            u32 slot_count;
            struct rt_scope_slot *slots;
            struct rt_astnode *child_scope;
            struct rt_astnode *expr;
        } scope;

        struct {
            u32 expr_count;
            struct rt_astnode **exprs;
        } block;

        struct {
            struct rt_any value;
        } literal;

        struct {
            const char *name;
        } get_local;

        struct {
            const char *name;
            struct rt_astnode *expr;
        } set_local;

        struct {
            const char *name;
        } get_const_global;

        struct {
            const char *name;
            struct rt_astnode *expr;
        } set_const_global;

        struct {
            struct rt_astnode *pred_expr;
            struct rt_astnode *then_expr;
            struct rt_astnode *else_expr;
        } cond;

        struct {
            struct rt_astnode *pred_expr;
            struct rt_astnode *body_expr;
        } loop;

        struct {
            struct rt_astnode *func_expr;
            struct rt_astnode **arg_exprs;
            u32 arg_count;
        } call;
    } u;
};


#endif
