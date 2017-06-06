#include "rt.h"

struct rt_any rt_weak_any(struct rt_any any) {
    struct rt_type *type = rt_any_get_type(any);
    if (type->kind != RT_KIND_PTR) {
        return any;
    }
    return rt_any_from_ptr(rt_gettype_weak(type), any.u.ptr);
}

bool rt_any_to_bool(struct rt_any a) {
    assert(rt_any_is_bool(a));
    return a.u._bool;
}

f64 rt_any_to_f64(struct rt_any a) {
    struct rt_type *type = rt_any_get_type(a);
    assert(type->kind == RT_KIND_REAL);
    if (type->size == 4) {
        return a.u.f32;
    }
    return a.u.f64;
}

u64 rt_any_to_u64(struct rt_any a) {
    struct rt_type *type = rt_any_get_type(a);
    assert(type->kind == RT_KIND_UNSIGNED);
    switch (type->size) {
    case 1: return a.u.u8;
    case 2: return a.u.u16;
    case 4: return a.u.u32;
    default:
    case 8: return a.u.u64;
    }
}

i64 rt_any_to_i64(struct rt_any a) {
    struct rt_type *type = rt_any_get_type(a);
    assert(type->kind == RT_KIND_SIGNED);
    switch (type->size) {
    case 1: return a.u.i8;
    case 2: return a.u.i16;
    case 4: return a.u.i32;
    default:
    case 8: return a.u.i64;
    }
}

struct rt_any rt_any_to_signed(struct rt_any a) {
    if (rt_any_is_unsigned(a)) {
        u64 uval = rt_any_to_u64(a);
        if (uval < INT64_MAX) {
            return rt_new_i64((i64)uval);
        }
    }
    return a;
}

struct rt_any rt_any_to_unsigned(struct rt_any a) {
    if (rt_any_is_signed(a)) {
        i64 ival = rt_any_to_i64(a);
        if (ival >= 0) {
            return rt_new_u64((u64)ival);
        }
    }
    return a;
}

bool rt_any_equals(struct rt_any a, struct rt_any b) {
    if (!a._type && !b._type) {
        return true;
    }
    if (!a._type || !b._type) {
        return false;
    }
    if (a._type->kind != b._type->kind) {
        if (a._type->kind == RT_KIND_UNSIGNED) {
            b = rt_any_to_unsigned(b);
            if (b._type->kind == RT_KIND_SIGNED) {
                a = rt_any_to_signed(a);
            }
        } else if (b._type->kind == RT_KIND_UNSIGNED) {
            a = rt_any_to_unsigned(a);
            if (a._type->kind == RT_KIND_SIGNED) {
                b = rt_any_to_signed(b);
            }
        } else {
            return false;
        }
        if (a._type->kind != b._type->kind) {
            return false;
        }
    }
    switch (a._type->kind) {
    case RT_KIND_PTR:
        return a.u.ptr == b.u.ptr;
    case RT_KIND_BOOL:
        return rt_any_to_bool(a) == rt_any_to_bool(b);
    case RT_KIND_SIGNED:
        return rt_any_to_i64(a) == rt_any_to_i64(b);
    case RT_KIND_UNSIGNED:
        return rt_any_to_u64(a) == rt_any_to_u64(b);
    case RT_KIND_REAL:
        return rt_any_to_f64(a) == rt_any_to_f64(b);
    case RT_KIND_FUNC:
        // TODO: func equality?
    default:
        return false;
    }
}
