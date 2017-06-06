#include "rt.h"
#include "hashtable.h"

#include <string.h>
#include <stdlib.h>

struct rt_symbol_index rt_symbols;
struct rt_type_index rt_types;

struct rt_any rt_nil;


DECL_HASH_TABLE(typemap, struct rt_symbol *, struct rt_type *)
IMPL_HASH_TABLE(typemap, struct rt_symbol *, struct rt_type *, hashutil_ptr_hash, hashutil_ptr_equals)

static struct typemap typemap;


#define DEF_SIMPLE_TYPE_FULL(Type, Name, ProperName, Kind) \
    rt_symbols.Name = rt_get_symbol(#ProperName); \
    rt_types.Name = rt_gettype_simple(Kind, sizeof(Type)); \
    typemap_put(&typemap, rt_symbols.Name.u.symbol, rt_types.Name);

#define DEF_SIMPLE_TYPE(Type, Name, Kind) \
    DEF_SIMPLE_TYPE_FULL(Type, Name, Name, Kind)

void rt_init_types() {
    /* string embeds a char array, "subtyping" it, and therefore has identical memory layout */
    struct rt_struct_field string_fields[1] = {{ rt_gettype_array(rt_gettype_simple(RT_KIND_UNSIGNED, sizeof(u8)), 0), "chars", 0 }};
    rt_types.string = rt_gettype_struct("string", 0, 1, string_fields);
    rt_types.boxed_string = rt_gettype_boxed(rt_types.string);

    /* symbol embeds string so they have equal memory layout */
    struct rt_struct_field symbol_fields[1] = {{ rt_types.string, "string", 0 }};
    rt_types.symbol = rt_gettype_struct("symbol", 0, 1, symbol_fields);
    rt_types.ptr_symbol = rt_gettype_ptr(rt_types.symbol);

    DEF_SIMPLE_TYPE(struct rt_any, any, RT_KIND_ANY);
    DEF_SIMPLE_TYPE(void *, nil, RT_KIND_NIL);

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
    rt_types.cons = rt_gettype_struct("cons", sizeof(struct rt_cons), 2, cons_fields);
    rt_types.boxed_cons = rt_gettype_boxed(rt_types.cons);
}

struct rt_type *rt_lookup_simple_type(struct rt_any sym) {
    assert(rt_any_is_symbol(sym));
    struct rt_type *result;
    if (typemap_get(&typemap, sym.u.symbol, &result)) {
        return result;
    }
    return NULL;
}


struct rt_any rt_new_cons(struct rt_thread_ctx *ctx, struct rt_any car, struct rt_any cdr) {
    struct rt_cons *cons = rt_gc_alloc(ctx, sizeof(struct rt_cons));
    cons->car = car;
    cons->cdr = cdr;
    return rt_any_from_cons(cons);
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
    return rt_any_from_ptr(ptr_type, array);
}

struct rt_any rt_new_string(struct rt_thread_ctx *ctx, const char *str) {
    rt_size_t length = strlen(str);
    struct rt_string *string = rt_gc_alloc(ctx, sizeof(struct rt_string) + length + 1);
    string->length = length;
    memcpy(string->data, str, length + 1);
    return rt_any_from_string(string);
}


DECL_HASH_TABLE(symtab, const char *, struct rt_symbol *)
IMPL_HASH_TABLE(symtab, const char *, struct rt_symbol *, hashutil_str_hash, hashutil_str_equals)

static struct symtab symtab;

/* TODO: add locking around symtab access if threading becomes a thing */

struct rt_any rt_get_symbol(const char *str) {
    struct rt_symbol *sym;
    if (!symtab_get(&symtab, str, &sym)) {
        rt_size_t length = strlen(str);
        sym = calloc(1, sizeof(struct rt_symbol) + length + 1);
        sym->length = length;
        memcpy(sym->data, str, length + 1);
        symtab_put(&symtab, (char *)sym->data, sym);
    }
    return rt_any_from_symbol(sym);
}
