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

static void rt_gc_mark_value(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type);

static void rt_gc_mark_box(struct rt_thread_ctx *ctx, struct rt_box *box, struct rt_type *boxed_type) {
    if (!rt_boxheader_is_marked(box->header)) {
        rt_boxheader_set_mark(box->header);
        rt_gc_mark_value(ctx, (char *)(box + 1), boxed_type);
    }
}

static void rt_gc_mark_struct(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type) {
    u32 field_count = type->u._struct.field_count;
    struct rt_struct_field *fields = type->u._struct.fields;
    
    for (u32 i = 0; i < field_count; ++i) {
        struct rt_struct_field *field = fields + i;
        rt_gc_mark_value(ctx, ptr + field->offset, field->type);
    }
}

static void rt_gc_add_weakptr(struct rt_thread_ctx *ctx, void **ptr, struct rt_type **any_type, struct rt_type *type) {
    if (ctx->num_weakptrs == ctx->max_weakptrs) {
        ctx->max_weakptrs = ctx->max_weakptrs ? ctx->max_weakptrs * 2 : 16;
        ctx->weakptrs = realloc(ctx->weakptrs, sizeof(struct rt_weakptr_entry) * ctx->max_weakptrs);
    }
    struct rt_weakptr_entry *e = &ctx->weakptrs[ctx->num_weakptrs++];
    e->ptr = ptr;
    e->any_type = any_type;
    e->type = type;
}

static void rt_gc_mark_array(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type) {
    struct rt_type *elem_type = type->u.array.elem_type;
    rt_size_t elem_size = elem_type->size;
    assert(elem_size);
    
    rt_size_t length;
    if (type->size) {
        length = type->size / elem_size;
    } else {
        /* an unsized array starts with a length */
        length = *(rt_size_t *)ptr;
        ptr += sizeof(rt_size_t);
    }
    
    for (rt_size_t i = 0; i < length; ++i) {
        rt_gc_mark_value(ctx, ptr + i*elem_size, elem_type);
    }
}

static void rt_gc_mark_value(struct rt_thread_ctx *ctx, char *ptr, struct rt_type *type) {
    if (!(type->flags & RT_TYPE_FLAG_NEED_GC_MARK)) {
        return;
    }
    switch (type->kind) {
    case RT_KIND_ANY: {
        struct rt_any *any = (struct rt_any *)ptr;
        if (any->_type) {
            if (any->_type->flags & RT_TYPE_FLAG_WEAK_PTR) {
                rt_gc_add_weakptr(ctx, &any->u.ptr, &any->_type, any->_type);
            } else {
                rt_gc_mark_value(ctx, (char *)&any->u.data, any->_type);
            }
        }
        break;
    }
    case RT_KIND_PTR:
        if (!*(char **)ptr) {
            break;
        }
        if (type->u.ptr.box_type) {
            if (type->flags & RT_TYPE_FLAG_WEAK_PTR) {
                rt_gc_add_weakptr(ctx, (void **)ptr, NULL, type);
            } else {
                rt_gc_mark_box(ctx,
                    (struct rt_box *)(*(char **)ptr - type->u.ptr.box_offset - sizeof(struct rt_box)),
                    type->u.ptr.box_type);
            }
        } else {
            rt_gc_mark_value(ctx, *(char **)ptr, type->u.ptr.target_type);
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


static void free_boxes(struct rt_thread_ctx *ctx, struct rt_box *boxes) {
    while (boxes) {
        struct rt_box *box = boxes;
        boxes = rt_boxheader_get_next(box->header);
        if (ctx->free_func) {
            ctx->free_func(ctx->free_func_userdata, box);
        } else {
            free(box);
        }
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
            rt_gc_mark_value(ctx, root, type);
        }
        roots = roots[0];
    }

    /* TODO: make hash table play nice with GC so we don't have to mark the keys manually */
    struct rt_module *module = ctx->current_module;
    if (module) {
        for (u32 i = 0; i < module->sourcemap.size; ++i) {
            struct rt_sourcemap_entry *e = module->sourcemap.entries + i;
            if (e->hash) {
                rt_gc_mark_value(ctx, (char *)&e->key, rt_types.boxed_cons);
            }
        }
    }

    /* null out the weak pointers */
    for (u32 i = 0; i < ctx->num_weakptrs; ++i) {
        struct rt_weakptr_entry e = ctx->weakptrs[i];
        struct rt_box *box = (struct rt_box *)(*(char **)e.ptr - e.type->u.ptr.box_offset - sizeof(struct rt_box));
        if (!rt_boxheader_is_marked(box->header)) {
            *e.ptr = NULL;
            if (e.any_type) {
                *e.any_type = NULL;
            }
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
    free_boxes(ctx, unreachable);
}

void rt_gc_free_all(struct rt_thread_ctx *ctx) {
    free_boxes(ctx, ctx->boxes);
    ctx->boxes = NULL;
}
