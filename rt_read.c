#include "rt.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

IMPL_HASH_TABLE(rt_sourcemap, struct rt_cons *, struct rt_sourceloc, hashutil_ptr_hash, hashutil_ptr_equals)


long long int my_strtoll(const char *nptr, const char **endptr, int base);
double my_strtod(const char *string, const char **endPtr);


static bool is_upper(char ch) { return ch >= 'A' && ch <= 'Z'; }

static bool is_lower(char ch) { return ch >= 'a' && ch <= 'z'; }

static bool is_alpha(char ch) { return is_upper(ch) || is_lower(ch); }

static bool is_digit(char ch) { return ch >= '0' && ch <= '9'; }

static bool is_alphanum(char ch) { return is_alpha(ch) || is_digit(ch); }

static bool is_symchar(char ch) {
    switch (ch) {
    case '_':
    case '-':
    case '=':
    case '+':
    case '*':
    case '/':
    case '?':
    case '!':
    case '&':
    case '%':
    case '^':
    case '~':
        return true;
    default:
        return false;
    }
}


#define SCRATCH_LEN 1024

struct reader_state {
    struct rt_thread_ctx *ctx;
    struct rt_module *mod;

    const char *text;
    u32 pos;
    struct rt_sourceloc loc;

    char scratch[SCRATCH_LEN];
};


static void read_error(struct reader_state *state, const char *fmt, ...) {
    printf("line %d, col %d: ", state->loc.line + 1, state->loc.col + 1);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    exit(1);
}

static char peek(struct reader_state *state, int offset) {
    return state->text[state->pos + offset];
}

static void step(struct reader_state *state) {
    ++state->loc.col;
    ++state->pos;
}

static void spacestep(struct reader_state *state) {
    if (state->text[state->pos] == '\r') {
        if (state->text[state->pos + 1] != '\n') {
            ++state->loc.line;
            state->loc.col = 0;
            ++state->pos;
            return;
        }
    } else if (state->text[state->pos] == '\n') {
        ++state->loc.line;
        state->loc.col = 0;
        ++state->pos;
        return;
    }
    ++state->loc.col;
    ++state->pos;
}

static void skip_space(struct reader_state *state) {
    char ch;
    for (;;) {
        switch (peek(state, 0)) {
        case ' ':
        case '\t':
        case '\f':
        case '\v':
        case '\r':
        case '\n':
            spacestep(state);
            continue;
        case ';': // line comment
            do {
                spacestep(state);
                ch = peek(state, 0);
            } while (ch && ch != '\n' && ch != '\r');
            continue;
        default:
            return;
        }
    }
}

static void expect_delim(struct reader_state *state) {
    switch (peek(state, 0)) {
    case ' ':
    case '\t':
    case '\f':
    case '\v':
    case '\r':
    case '\n':
    case '.':
    case ':':
    case '(':
    case ')':
    case '[':
    case ']':
        return;
    }
    read_error(state, "expected delimiter after expression");
}

static struct rt_any read_string(struct reader_state *state) {
    char *scratch = state->scratch;
    uint32_t len = 0;
    for (;;) {
        char ch = peek(state, 0);
        if (ch == '"') {
            step(state);
            if (len >= SCRATCH_LEN) {
                read_error(state, "string is too long");
            }
            scratch[len] = '\0';
            return rt_new_string(state->ctx, scratch);
        } else if (ch == '\\') {
            step(state);
            ch = peek(state, 0);
            if (ch == '\0') {
                read_error(state, "unexpected end of input while reading string");
            }
            if (len >= SCRATCH_LEN) {
                read_error(state, "string is too long");
            }
            switch (ch) {
            case '\'': scratch[len++] = '\''; break;
            case '"': scratch[len++] = '"'; break;
            case '?': scratch[len++] = '?'; break;
            case '\\': scratch[len++] = '\\'; break;
            case 'a': scratch[len++] = '\a'; break;
            case 'b': scratch[len++] = '\b'; break;
            case 'f': scratch[len++] = '\f'; break;
            case 'n': scratch[len++] = '\r'; break;
            case 'r': scratch[len++] = '\r'; break;
            case 't': scratch[len++] = '\t'; break;
            case 'v': scratch[len++] = '\v'; break;
            // TODO: handle \nnn \xnn \unnnn \Unnnnnnnn
            default: read_error(state, "unexpected escape char: %c", ch);
            }
            step(state);
        } else if (ch == '\0') {
            read_error(state, "unexpected end of input while reading string");
        } else {
            if (ch == '\r' || ch == '\n') {
                spacestep(state);
            } else {
                step(state);
            }
            if (len >= SCRATCH_LEN) {
                read_error(state, "string is too long");
            }
            scratch[len++] = ch;
        }
    }
    return rt_nil;
}

static struct rt_any read_symbol(struct reader_state *state) {
    char *scratch = state->scratch;
    uint32_t len = 0;
    for (;;) {
        char ch = peek(state, 0);
        if (!is_alphanum(ch) && !is_symchar(ch)) {
            if (len == 0) {
                read_error(state, "expected a symbol");
            }
            if (len >= SCRATCH_LEN) {
                read_error(state, "string is too long");
            }
            scratch[len] = '\0';
            struct rt_any result = rt_get_symbol(scratch);
            return result;
        }
        if (len >= SCRATCH_LEN) {
            read_error(state, "string is too long");
        }
        scratch[len++] = ch;
        step(state);
    }
}

static struct rt_any read_number(struct reader_state *state) {
    errno = 0;
    const char *end;
    const char *start = state->text + state->pos;
    // TODO: handle unsigned numbers
    i64 llval = my_strtoll(start, &end, 0);
    if (start == end) {
        read_error(state, "error parsing number");
    }
    if (errno == ERANGE) {
        read_error(state, "number too large");
    }
    if (*end != '.') {
        state->pos += (int)(end - start);
        return rt_new_i64(llval);
    }
    start = state->text + state->pos;
    f64 dval = my_strtod(start, &end);
    if (start == end) {
        read_error(state, "error parsing number");
    }
    if (errno == ERANGE) {
        read_error(state, "number too large");
    }
    state->pos += (int)(end - start);
    return rt_new_f64(dval);
}

static struct rt_any read_form(struct reader_state *state);

static struct rt_any read_list(struct reader_state *state, char end) {
    skip_space(state);
    if (peek(state, 0) == end) {
        step(state);
        return rt_nil;
    }

    struct rt_sourceloc orig_loc = state->loc;
    struct rt_any form = read_form(state);
    struct rt_any result = rt_new_cons(state->ctx, form, read_list(state, end));
    if (state->mod) {
        // store location of all car forms, using the containing cons as key
        rt_sourcemap_put(&state->mod->sourcemap, result.u.ptr, orig_loc);
    }
    return result;
}

static struct rt_any read_form(struct reader_state *state) {
    struct rt_thread_ctx *ctx = state->ctx;
    struct rt_any result = rt_nil;
    skip_space(state);
    char ch = peek(state, 0);
    if (ch == '(') {
        step(state);
        result = read_list(state, ')');
    } else if (ch == '#') {
        step(state);
        ch = peek(state, 0);
        if (ch == 't') {
            step(state);
            expect_delim(state);
            result = rt_new_bool(true);
        } else if (ch == 'f') {
            step(state);
            expect_delim(state);
            result = rt_new_bool(false);
        } else {
            read_error(state, "expected #t or #f");
        }
    } else if (ch == '\'') {
        step(state);
        struct rt_any form = read_form(state);
        result = rt_new_cons(ctx, rt_get_symbol("quote"), form);
    } else if (ch == '"') {
        step(state);
        result = read_string(state);
    } else if (is_alpha(ch) || is_symchar(ch)) {
        result = read_symbol(state);
    } else if (is_digit(ch) || ((ch == '+' || ch == '-') && is_digit(peek(state, 1)))) {
        result = read_number(state);
        expect_delim(state);
    } else {
        read_error(state, "expected an expression");
    }
    for (;;) {
        skip_space(state);
        ch = peek(state, 0);
        if (ch == '.') {
            step(state);
            skip_space(state);
            struct rt_any sym = read_symbol(state);
            result = rt_new_cons(ctx, rt_get_symbol("."), rt_new_cons(ctx, sym, rt_new_cons(ctx, result, rt_nil)));
        } else if (ch == '[') {
            step(state);
            struct rt_any list = read_list(state, ']');
            result = rt_new_cons(ctx, result, list);
        } else {
            break;
        }
    }
    if (ch == ':') {
        step(state);
        struct rt_any typeform = read_form(state);
        result = rt_new_cons(ctx, rt_get_symbol(":"), rt_new_cons(ctx, result, rt_new_cons(ctx, typeform, rt_nil)));
    }
    return result;
}

struct rt_any rt_read(struct rt_thread_ctx *ctx, const char *text) {
    struct reader_state state = {ctx,};
    state.text = text;
    return read_form(&state);
}
