#include "rt.h"
#include "hashtable.h"

#include <string.h>
#include <stdlib.h>

void rt_gc_free_all(struct rt_task *task);
void rt_gettype_free_all();


struct rt_symbol_index rt_symbols;
struct rt_type_index rt_types;

struct rt_any rt_nil;


DECL_HASH_TABLE(typemap, struct rt_symbol *, struct rt_type *)
IMPL_HASH_TABLE(typemap, struct rt_symbol *, struct rt_type *, hashutil_ptr_hash, hashutil_ptr_equals)

/* TODO: add locking around typemap access if threading becomes a thing */
static struct typemap typemap;


DECL_HASH_TABLE(symtab, const char *, struct rt_symbol *)
IMPL_HASH_TABLE(symtab, const char *, struct rt_symbol *, hashutil_str_hash, hashutil_str_equals)

/* TODO: add locking around symtab access if threading becomes a thing */
static struct symtab symtab;


#define RT_INIT_TYPE(Type, VarName, ProperName, Kind, Flags) \
    rt_symbols.VarName = rt_get_symbol(#ProperName); \
    rt_types.VarName = rt_gettype_simple(Kind, sizeof(Type)); \
    rt_types.VarName->flags = Flags; \
    typemap_put(&typemap, rt_symbols.VarName.u.symbol, rt_types.VarName);

#define RT_INIT_SYMBOL_SHORTCUT(VarName, ProperName) \
    rt_symbols.VarName = rt_get_symbol(#ProperName);

void rt_init(void) {
    /* string embeds a char array, "subtyping" it, and therefore has identical memory layout */
    struct rt_struct_field string_fields[1] = {{ rt_gettype_array(rt_gettype_simple(RT_KIND_UNSIGNED, sizeof(u8)), 0), "chars", 0 }};
    rt_types.string = rt_gettype_struct("string", 0, 1, string_fields);
    rt_types.boxed_string = rt_gettype_boxed(rt_types.string);

    /* symbol embeds string so they have equal memory layout */
    struct rt_struct_field symbol_fields[1] = {{ rt_types.string, "string", 0 }};
    rt_types.symbol = rt_gettype_struct("symbol", 0, 1, symbol_fields);
    rt_types.ptr_symbol = rt_gettype_ptr(rt_types.symbol);

    RT_FOREACH_SIMPLE_TYPE(RT_INIT_TYPE)
    RT_FOREACH_SYMBOL_SHORTCUT(RT_INIT_SYMBOL_SHORTCUT)

    struct rt_struct_field cons_fields[2] = {
        { rt_types.any, "car", offsetof(struct rt_cons, car) },
        { rt_types.any, "cdr", offsetof(struct rt_cons, cdr) },
    };
    rt_types.cons = rt_gettype_struct("cons", sizeof(struct rt_cons), 2, cons_fields);
    rt_types.boxed_cons = rt_gettype_boxed(rt_types.cons);
}

void rt_cleanup(void) {
    rt_gettype_free_all();

    typemap_free(&typemap);
    
    for (u32 i = 0; i < symtab.size; ++i) {
        struct symtab_entry *e = symtab.entries + i;
        if (e->hash) {
            free(e->value);
        }
    }
    symtab_free(&symtab);
}

void rt_task_cleanup(struct rt_task *task) {
    if (task->current_module) {
        rt_sourcemap_free(&task->current_module->location_before_car);
        rt_sourcemap_free(&task->current_module->location_after_car);
        rt_symbolmap_free(&task->current_module->symbolmap);
    }
    free(task->weakptrs);
    rt_gc_free_all(task);
    *task = (struct rt_task) {0,};
}


struct rt_type *rt_lookup_simple_type(struct rt_any sym) {
    assert(rt_any_is_symbol(sym));
    struct rt_type *result;
    if (typemap_get(&typemap, sym.u.symbol, &result)) {
        return result;
    }
    return NULL;
}


struct rt_any rt_new_cons(struct rt_task *task, struct rt_any car, struct rt_any cdr) {
    struct rt_cons *cons = rt_gc_alloc(task, sizeof(struct rt_cons));
    cons->car = car;
    cons->cdr = cdr;
    return rt_any_from_cons(cons);
}

struct rt_any rt_new_array(struct rt_task *task, rt_size_t length, struct rt_type *ptr_type) {
    assert(ptr_type->kind == RT_KIND_PTR);
    assert(ptr_type->u.ptr.box_type);
    assert(!ptr_type->u.ptr.box_offset);
    
    struct rt_type *array_type = ptr_type->u.ptr.box_type;
    assert(array_type->kind == RT_KIND_ARRAY);
    assert(!array_type->size); /* this boxed array must be unsized (have length stored in box, not type) */
    
    struct rt_type *elem_type = array_type->u.array.elem_type;
    rt_size_t elem_size = elem_type->size;
    assert(elem_size);
    
    void *array = rt_gc_alloc(task, sizeof(rt_size_t) + length*elem_size);
    *(rt_size_t *)array = length;
    return rt_any_from_ptr(ptr_type, array);
}

struct rt_any rt_new_string(struct rt_task *task, const char *str) {
    rt_size_t length = strlen(str);
    struct rt_string *string = rt_gc_alloc(task, sizeof(struct rt_string) + length + 1);
    string->length = length;
    memcpy(string->data, str, length + 1);
    return rt_any_from_string(string);
}

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
