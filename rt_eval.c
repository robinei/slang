#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "hashtable.h"
#include "murmur3.h"

static uint32_t str_hash(const char *key) {
    uint32_t hash;
    MurmurHash3_x86_32(key, strlen(key), 0, &hash);
    return hash;
}
static int str_equals(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}
DECL_HASH_TABLE(valuemap, const char *, struct rt_any)
IMPL_HASH_TABLE(valuemap, const char *, struct rt_any, str_hash, str_equals)


struct eval_state {
    struct rt_thread_ctx *ctx;
    struct valuemap globals;
};

static void eval_error(struct rt_astnode *node, const char *fmt, ...) {
    printf("line %d, col %d: ", node->sourceloc.line + 1, node->sourceloc.col + 1);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    exit(1);
}

static struct rt_any rt_ast_eval_expr(struct eval_state *state, struct rt_astnode *node) {
    struct rt_any result = rt_nil;
    switch (node->node_type) {
    case RT_ASTNODE_FUNC:
    case RT_ASTNODE_SCOPE:
    case RT_ASTNODE_BLOCK:
        for (u32 i = 0; i < node->u.block.expr_count; ++i) {
            result = rt_ast_eval_expr(state, node->u.block.exprs[i]);
        }
        break;
    case RT_ASTNODE_LITERAL: {
        result = node->u.literal.value;
        break;
    }
    case RT_ASTNODE_GET_LOCAL:
    case RT_ASTNODE_SET_LOCAL:
    case RT_ASTNODE_GET_CONST_GLOBAL:
        valuemap_get(&state->globals, node->u.get_const_global.name, &result);
        break;
    case RT_ASTNODE_DEF_CONST_GLOBAL:
        eval_error(node, "can only define globals at toplevel");
        break;
    case RT_ASTNODE_COND: {
        struct rt_any pred_result = rt_ast_eval_expr(state, node->u.cond.pred_expr);
        if (pred_result.type->kind != RT_KIND_BOOL) {
            eval_error(node, "boolean value required for conditional predicate");
            break;
        }
        b32 val = rt_any_to_b32(pred_result);
        if (!val) {
            result = rt_ast_eval_expr(state, node->u.cond.else_expr);
        } else {
            result = rt_ast_eval_expr(state, node->u.cond.then_expr);
        }
        break;
    }
    case RT_ASTNODE_LOOP: {
        for (;;) {
            struct rt_any pred_result = rt_ast_eval_expr(state, node->u.loop.pred_expr);
            if (pred_result.type->kind != RT_KIND_BOOL) {
                eval_error(node, "boolean value required for loop predicate");
                break;
            }
            b32 val = rt_any_to_b32(pred_result);
            if (!val) {
                break;
            }
            result = rt_ast_eval_expr(state, node->u.loop.body_expr);
        }
    }
    case RT_ASTNODE_CALL: {
        struct rt_any func_result = rt_ast_eval_expr(state, node->u.call.func_expr);
        if (func_result.type->kind != RT_KIND_FUNC) {
            eval_error(node, "expected a callable value");
            break;
        }

    }
    }
    return result;
}

static struct rt_any rt_ast_eval_toplevel(struct eval_state *state, struct rt_astnode *node) {
    struct rt_any result = rt_nil;
    switch (node->node_type) {
    case RT_ASTNODE_BLOCK:
        for (u32 i = 0; i < node->u.block.expr_count; ++i) {
            result = rt_ast_eval_toplevel(state, node->u.block.exprs[i]);
        }
        break;
    case RT_ASTNODE_GET_CONST_GLOBAL:
        if (!valuemap_get(&state->globals, node->u.get_const_global.name, &result)) {
            eval_error(node, "no toplevel item with name '%s' found", node->u.get_const_global.name);
        }
        break;
    case RT_ASTNODE_DEF_CONST_GLOBAL: {
        struct rt_any dummy;
        if (valuemap_get(&state->globals, node->u.set_const_global.name, &result)) {
            eval_error(node, "redefinition of already defined toplevel name: %s", node->u.set_const_global.name);
            break;
        }
        result = rt_ast_eval_expr(state, node->u.set_const_global.expr);
        valuemap_put(&state->globals, node->u.set_const_global.name, result);
        break;
    }
    default:
        eval_error(node, "unexpected toplevel form");
        break;
    }
    return result;
}
