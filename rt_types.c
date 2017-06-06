#include "rt.h"
#include "hashtable.h"
#include "murmur3.h"

#include <string.h>
#include <stdlib.h>

struct rt_symbol_index rt_symbols;
struct rt_type_index rt_types;

struct rt_any rt_nil;


static u32 pointer_hash(struct rt_symbol *ptr) {
    u32 val = (u32)(intptr_t)ptr;
    val = ~val + (val << 15);
    val = val ^ (val >> 12);
    val = val + (val << 2);
    val = val ^ (val >> 4);
    val = val * 2057;
    val = val ^ (val >> 16);
    return val;
}
static bool pointer_equals(struct rt_symbol *a, struct rt_symbol *b) {
    return a == b;
}
DECL_HASH_TABLE(typemap, struct rt_symbol *, struct rt_type *)
IMPL_HASH_TABLE(typemap, struct rt_symbol *, struct rt_type *, pointer_hash, pointer_equals)

static struct typemap typemap;


#define DEF_SIMPLE_TYPE_FULL(type, name, propername, kind) \
    rt_symbols.name = rt_get_symbol(#propername); \
    rt_types.name = rt_gettype_simple(kind, sizeof(type)); \
    typemap_put(&typemap, rt_symbols.name.u.ptr, rt_types.name);

#define DEF_SIMPLE_TYPE(type, name, kind) \
    DEF_SIMPLE_TYPE_FULL(type, name, #name, kind)

void rt_init_types() {
    struct rt_struct_field string_fields[1] = {{ rt_gettype_array(rt_gettype_simple(RT_KIND_UNSIGNED, sizeof(u8)), 0), "chars", 0 }};
    rt_types.string = rt_gettype_struct(0, 1, string_fields);
    rt_types.boxed_string = rt_gettype_boxed(rt_types.string);

    struct rt_struct_field symbol_fields[1] = {{ rt_types.string, "string", 0 }};
    rt_types.symbol = rt_gettype_struct(0, 1, symbol_fields);
    rt_types.ptr_symbol = rt_gettype_ptr(rt_types.symbol);

    DEF_SIMPLE_TYPE(struct rt_any, any, RT_KIND_ANY);

    DEF_SIMPLE_TYPE(u8, u8, RT_KIND_UNSIGNED);
    DEF_SIMPLE_TYPE(u16, u16, RT_KIND_UNSIGNED);
    DEF_SIMPLE_TYPE(u32, u32, RT_KIND_UNSIGNED);
    DEF_SIMPLE_TYPE(u64, u64, RT_KIND_UNSIGNED);

    DEF_SIMPLE_TYPE(i8, i8, RT_KIND_SIGNED);
    DEF_SIMPLE_TYPE(i16, i16, RT_KIND_SIGNED);
    DEF_SIMPLE_TYPE(i32, i32, RT_KIND_SIGNED);
    DEF_SIMPLE_TYPE(i64, i64, RT_KIND_SIGNED);

    DEF_SIMPLE_TYPE(f32, f32, RT_KIND_REAL);
    DEF_SIMPLE_TYPE(f64, f64, RT_KIND_REAL);

    DEF_SIMPLE_TYPE_FULL(bool, _bool, "bool", RT_KIND_BOOL);

    struct rt_struct_field cons_fields[2] = {
        { rt_types.any, "car", offsetof(struct rt_cons, car) },
        { rt_types.any, "cdr", offsetof(struct rt_cons, cdr) },
    };
    rt_types.cons = rt_gettype_struct(sizeof(struct rt_cons), 2, cons_fields);
    rt_types.boxed_cons = rt_gettype_boxed(rt_types.cons);
}

struct rt_type *rt_lookup_simple_type(struct rt_any sym) {
    assert(sym.type == rt_types.ptr_symbol);
    struct rt_type *result;
    if (typemap_get(&typemap, sym.u.ptr, &result)) {
        return result;
    }
    return NULL;
}

struct rt_any rt_new_cons(struct rt_thread_ctx *ctx, struct rt_any car, struct rt_any cdr) {
    struct rt_cons *cons = rt_gc_alloc(ctx, sizeof(struct rt_cons));
    cons->car = car;
    cons->cdr = cdr;

    struct rt_any any;
    any.type = rt_types.boxed_cons;
    any.u.ptr = cons;
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
    
    void *array = rt_gc_alloc(ctx, sizeof(rt_size_t) + length*elem_size);
    *(rt_size_t *)array = length;

    struct rt_any any;
    any.type = ptr_type;
    any.u.ptr = array;
    return any;
}

struct rt_any rt_new_string(struct rt_thread_ctx *ctx, const char *str) {
    rt_size_t length = strlen(str);
    struct rt_string *string = rt_gc_alloc(ctx, sizeof(struct rt_string) + length + 1);
    string->chars.length = length;
    memcpy(string->chars.data, str, length + 1);

    struct rt_any any;
    any.type = rt_types.boxed_string;
    any.u.ptr = string;
    return any;
}


static uint32_t str_hash(const char *key) {
    uint32_t hash;
    MurmurHash3_x86_32(key, strlen(key), 0, &hash);
    return hash;
}
static int str_equals(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}
DECL_HASH_TABLE(symtab, const char *, struct rt_symbol *)
IMPL_HASH_TABLE(symtab, const char *, struct rt_symbol *, str_hash, str_equals)

static struct symtab symtab;

/* TODO: add locking around symtab access if threading becomes a thing */

struct rt_any rt_get_symbol(const char *str) {
    struct rt_symbol *sym;
    if (!symtab_get(&symtab, str, &sym)) {
        rt_size_t length = strlen(str);
        sym = calloc(1, sizeof(struct rt_symbol) + length + 1);
        sym->string.chars.length = length;
        memcpy(sym->string.chars.data, str, length + 1);
        symtab_put(&symtab, sym->string.chars.data, sym);
    }
    struct rt_any any;
    any.type = rt_types.ptr_symbol;
    any.u.ptr = sym;
    return any;
}
