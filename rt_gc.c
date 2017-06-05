#include "rt.h"

#include <stdlib.h>
#include <stdio.h>

#define rt_boxheader_get_next(h) ((struct rt_box *)((h) & ~(uintptr_t)1))
#define rt_boxheader_set_next(h, next) do { (h) = (uintptr_t)(next) | ((h) & (uintptr_t)1); } while(0)
#define rt_boxheader_is_marked(h) ((h) & 1)
#define rt_boxheader_set_mark(h) do { (h) |= 1; } while(0)
#define rt_boxheader_clear_mark(h) do { (h) &= ~(uintptr_t)1; } while(0)

void *rt_gc_alloc(struct rt_thread_ctx *ctx, rt_size_t size) {
    struct rt_box *box = (struct rt_box *)calloc(1, sizeof(struct rt_box) + size);
    rt_boxheader_set_next(box->header, ctx->boxes);
    ctx->boxes = box;
    return box + 1;
}

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
        /* an unsized array starts with a length */
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

    /* TODO: make hash table play nice with GC so we don't have to mark the keys manually */
    for (u32 i = 0; i < ctx->sourcemap.size; ++i) {
        struct rt_sourcemap_entry *e = ctx->sourcemap.entries + i;
        if (e->hash) {
            rt_gc_mark_single(ctx, (char *)&e->key, rt_types.boxed_cons);
        }
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