#include "rt.h"

struct rt_any rt_weak_any(struct rt_any any) {
    if (!any.type) {
        return any;
    }
    if (any.type->kind != RT_KIND_PTR) {
        return any;
    }
    struct rt_any new_any;
    new_any.type = rt_gettype_weak(any.type);
    new_any.u.ptr = any.u.ptr;
    return new_any;
}

static b32 rt_any_to_b32(struct rt_any a) {
    assert(a.type->kind == RT_KIND_BOOL);
    if (a.type->size == 1) {
        return a.u.b8;
    }
    return a.u.b32;
}

static f64 rt_any_to_f64(struct rt_any a) {
    assert(a.type->kind == RT_KIND_REAL);
    if (a.type->size == 4) {
        return a.u.f32;
    }
    return a.u.f64;
}

static u64 rt_any_to_u64(struct rt_any a) {
    assert(a.type->kind == RT_KIND_UNSIGNED);
    switch (a.type->size) {
    case 1: return a.u.u8;
    case 2: return a.u.u16;
    case 4: return a.u.u32;
    default:
    case 8: return a.u.u64;
    }
}

static i64 rt_any_to_i64(struct rt_any a) {
    assert(a.type->kind == RT_KIND_SIGNED);
    switch (a.type->size) {
    case 1: return a.u.i8;
    case 2: return a.u.i16;
    case 4: return a.u.i32;
    default:
    case 8: return a.u.i64;
    }
}

static struct rt_any rt_any_to_signed(struct rt_any a) {
    if (a.type->kind == RT_KIND_UNSIGNED) {
        u64 uval = rt_any_to_u64(a);
        if (uval < INT64_MAX) {
            return rt_new_i64((i64)uval);
        }
    }
    return a;
}

static struct rt_any rt_any_to_unsigned(struct rt_any a) {
    if (a.type->kind == RT_KIND_SIGNED) {
        i64 ival = rt_any_to_i64(a);
        if (ival >= 0) {
            return rt_new_u64((u64)ival);
        }
    }
    return a;
}

b32 rt_any_equals(struct rt_any a, struct rt_any b) {
    if (!a.type && !b.type) {
        return TRUE;
    }
    if (!a.type || !b.type) {
        return FALSE;
    }
    if (a.type->kind != b.type->kind) {
        if (a.type->kind == RT_KIND_UNSIGNED) {
            b = rt_any_to_unsigned(b);
            if (b.type->kind == RT_KIND_SIGNED) {
                a = rt_any_to_signed(a);
            }
        } else if (b.type->kind == RT_KIND_UNSIGNED) {
            a = rt_any_to_unsigned(a);
            if (a.type->kind == RT_KIND_SIGNED) {
                b = rt_any_to_signed(b);
            }
        } else {
            return FALSE;
        }
        if (a.type->kind != b.type->kind) {
            return FALSE;
        }
    }
    switch (a.type->kind) {
    case RT_KIND_PTR:
        return a.u.ptr == b.u.ptr;
    case RT_KIND_BOOL:
        return rt_any_to_b32(a) == rt_any_to_b32(b);
    case RT_KIND_SIGNED:
        return rt_any_to_i64(a) == rt_any_to_i64(b);
    case RT_KIND_UNSIGNED:
        return rt_any_to_u64(a) == rt_any_to_u64(b);
    case RT_KIND_REAL:
        return rt_any_to_f64(a) == rt_any_to_f64(b);
    case RT_KIND_FUN:
    default:
        return FALSE;
    }
}
