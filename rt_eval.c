#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "hashtable.h"

IMPL_HASH_TABLE(rt_symbolmap, struct rt_symbol *, struct rt_astnode *, hashutil_ptr_hash, hashutil_ptr_equals)


struct eval_state {
    struct rt_task *task;
    struct rt_module *mod;

    struct rt_any stack[65536];
    u32 stack_top;
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
    case RT_ASTNODE_LITERAL:
        result = node->const_value;
        break;
    case RT_ASTNODE_SCOPE:
        state->stack_top += node->u.scope.var_count;
        result = rt_ast_eval_expr(state, node->u.scope.expr);
        state->stack_top -= node->u.scope.var_count;
        break;
    case RT_ASTNODE_BLOCK:
        for (u32 i = 0; i < node->u.block.expr_count; ++i) {
            result = rt_ast_eval_expr(state, node->u.block.exprs[i]);
        }
        break;
    case RT_ASTNODE_GET_GLOBAL: {
        struct rt_astnode *temp;
        if (!rt_symbolmap_get(&state->mod->symbolmap, node->u.get_global.name, &temp)) {
            eval_error(node, "no toplevel item with name '%s' found", node->u.get_global.name);
        }
        result = temp->const_value;
        break;
    }
    case RT_ASTNODE_GET_LOCAL:
        result = state->stack[state->stack_top - node->u.get_local.stack_index];
        break;
    case RT_ASTNODE_SET_LOCAL:
        result = rt_ast_eval_expr(state, node->u.set_local.expr);
        state->stack[state->stack_top - node->u.set_local.stack_index] = result;
        break;
    case RT_ASTNODE_COND: {
        struct rt_any pred_result = rt_ast_eval_expr(state, node->u.cond.pred_expr);
        if (!rt_any_is_bool(pred_result)) {
            eval_error(node, "boolean value required for conditional predicate");
            break;
        }
        bool val = rt_any_to_bool(pred_result);
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
            if (!rt_any_is_bool(pred_result)) {
                eval_error(node, "boolean value required for loop predicate");
                break;
            }
            bool val = rt_any_to_bool(pred_result);
            if (!val) {
                break;
            }
            result = rt_ast_eval_expr(state, node->u.loop.body_expr);
        }
    }
    case RT_ASTNODE_CALL: {
        struct rt_any func_result = rt_ast_eval_expr(state, node->u.call.func_expr);
        if (!rt_any_is_func(func_result)) {
            eval_error(node, "expected a function value");
            break;
        }

        u32 arg_count = node->u.call.arg_count;
        struct rt_astnode *body = func_result.u.func->body_expr;

        assert(func_result._type->u.func.param_count == arg_count);
        if (arg_count > 0) {
            assert(body->node_type == RT_ASTNODE_SCOPE);
            assert(body->u.scope.var_count == arg_count);
        }

        u32 top = state->stack_top;
        for (u32 i = 0; i < arg_count; ++i) {
            struct rt_func_param *param = func_result._type->u.func.params + i;
            struct rt_any arg_result = rt_ast_eval_expr(state, node->u.call.arg_exprs[i]);
            if (rt_any_get_type(arg_result) != param->type) {
                eval_error(node, "type mismatch");
                break;
            }
            state->stack[top++] = arg_result;
        }

        result = rt_ast_eval_expr(state, body);
    }
    }
    return result;
}
