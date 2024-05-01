#ifndef _FLOAT_H_
#define _FLOAT_H_

#define FLT_RADIX 2

/* IEEE float */
#define FLT_MANT_DIG 24
#define FLT_DIG 6
#define FLT_ROUNDS 1
#define FLT_EPSILON 1.19209290e-07F
#define FLT_MIN_EXP (-125)
#define FLT_MIN 1.17549435e-38F
#define FLT_MIN_10_EXP (-37)
#define FLT_MAX_EXP 128
#define FLT_MAX 3.40282347e+38F
#define FLT_MAX_10_EXP 38

/* IEEE double */
#define DBL_MANT_DIG 53
#define DBL_DIG 15
#define DBL_EPSILON 2.2204460492503131e-16
#define DBL_MIN_EXP (-1021)
#define DBL_MIN 2.2250738585072014e-308
#define DBL_MIN_10_EXP (-307)
#define DBL_MAX_EXP 1024
#define DBL_MAX 1.7976931348623157e+308
#define DBL_MAX_10_EXP 308

/* horrible intel long double */
#if defined __i386__ || defined __x86_64__

#define LDBL_MANT_DIG 64
#define LDBL_DIG 18
#define LDBL_EPSILON 1.08420217248550443401e-19L
#define LDBL_MIN_EXP (-16381)
#define LDBL_MIN 3.36210314311209350626e-4932L
#define LDBL_MIN_10_EXP (-4931)
#define LDBL_MAX_EXP 16384
#define LDBL_MAX 1.18973149535723176502e+4932L
#define LDBL_MAX_10_EXP 4932
#define DECIMAL_DIG 21

#elif defined __aarch64__ || defined __riscv
/*
 * Use values from:
 * gcc -dM -E -xc /dev/null | grep LDBL | sed -e "s/__//g"
 */
#define LDBL_MANT_DIG 113
#define LDBL_DIG 33
#define LDBL_EPSILON 1.92592994438723585305597794258492732e-34L
#define LDBL_MIN_EXP (-16381)
#define LDBL_MIN 3.36210314311209350626267781732175260e-4932L
#define LDBL_MIN_10_EXP (-4931)
#define LDBL_MAX_EXP 16384
#define LDBL_MAX 1.18973149535723176508575932662800702e+4932L
#define LDBL_MAX_EXP 16384
#define DECIMAL_DIG 36

#else

/* same as IEEE double */
#define LDBL_MANT_DIG 53
#define LDBL_DIG 15
#define LDBL_EPSILON 2.2204460492503131e-16L
#define LDBL_MIN_EXP (-1021)
#define LDBL_MIN 2.2250738585072014e-308L
#define LDBL_MIN_10_EXP (-307)
#define LDBL_MAX_EXP 1024
#define LDBL_MAX 1.7976931348623157e+308L
#define LDBL_MAX_10_EXP 308
#define DECIMAL_DIG 17

#endif

#endif /* _FLOAT_H_ */
#ifndef _STDALIGN_H
#define _STDALIGN_H

#if __STDC_VERSION__ < 201112L && (defined(__GNUC__) || defined(__TINYC__))
# define _Alignas(t) __attribute__((__aligned__(t)))
# define _Alignof(t) __alignof__(t)
#endif

#define alignas _Alignas
#define alignof _Alignof

#define __alignas_is_defined 1
#define __alignof_is_defined 1

#endif /* _STDALIGN_H */

#ifndef _STDARG_H
#define _STDARG_H

typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_copy __builtin_va_copy
#define va_end __builtin_va_end

/* fix a buggy dependency on GCC in libio.h */
typedef va_list __gnuc_va_list;
#define _VA_LIST_DEFINED

#endif /* _STDARG_H */
/* This file is derived from clang's stdatomic.h */

/*===---- stdatomic.h - Standard header for atomic types and operations -----===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef _STDATOMIC_H
#define _STDATOMIC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

/* Memory ordering */
typedef enum {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST,
} memory_order;

/* Atomic typedefs */
typedef _Atomic(_Bool) atomic_bool;
typedef _Atomic(char) atomic_char;
typedef _Atomic(signed char) atomic_schar;
typedef _Atomic(unsigned char) atomic_uchar;
typedef _Atomic(short) atomic_short;
typedef _Atomic(unsigned short) atomic_ushort;
typedef _Atomic(int) atomic_int;
typedef _Atomic(unsigned int) atomic_uint;
typedef _Atomic(long) atomic_long;
typedef _Atomic(unsigned long) atomic_ulong;
typedef _Atomic(long long) atomic_llong;
typedef _Atomic(unsigned long long) atomic_ullong;
typedef _Atomic(uint_least16_t) atomic_char16_t;
typedef _Atomic(uint_least32_t) atomic_char32_t;
typedef _Atomic(wchar_t) atomic_wchar_t;
typedef _Atomic(int_least8_t) atomic_int_least8_t;
typedef _Atomic(uint_least8_t) atomic_uint_least8_t;
typedef _Atomic(int_least16_t) atomic_int_least16_t;
typedef _Atomic(uint_least16_t) atomic_uint_least16_t;
typedef _Atomic(int_least32_t) atomic_int_least32_t;
typedef _Atomic(uint_least32_t) atomic_uint_least32_t;
typedef _Atomic(int_least64_t) atomic_int_least64_t;
typedef _Atomic(uint_least64_t) atomic_uint_least64_t;
typedef _Atomic(int_fast8_t) atomic_int_fast8_t;
typedef _Atomic(uint_fast8_t) atomic_uint_fast8_t;
typedef _Atomic(int_fast16_t) atomic_int_fast16_t;
typedef _Atomic(uint_fast16_t) atomic_uint_fast16_t;
typedef _Atomic(int_fast32_t) atomic_int_fast32_t;
typedef _Atomic(uint_fast32_t) atomic_uint_fast32_t;
typedef _Atomic(int_fast64_t) atomic_int_fast64_t;
typedef _Atomic(uint_fast64_t) atomic_uint_fast64_t;
typedef _Atomic(intptr_t) atomic_intptr_t;
typedef _Atomic(uintptr_t) atomic_uintptr_t;
typedef _Atomic(size_t) atomic_size_t;
typedef _Atomic(ptrdiff_t) atomic_ptrdiff_t;
typedef _Atomic(intmax_t) atomic_intmax_t;
typedef _Atomic(uintmax_t) atomic_uintmax_t;

/* Atomic flag */
typedef struct {
    atomic_bool value;
} atomic_flag;

#define ATOMIC_FLAG_INIT {0}
#define ATOMIC_VAR_INIT(value) (value)

#define atomic_flag_test_and_set_explicit(object, order)                  \
    __atomic_test_and_set((void *)(&((object)->value)), order)
#define atomic_flag_test_and_set(object)                                  \
    atomic_flag_test_and_set_explicit(object, __ATOMIC_SEQ_CST)

#define atomic_flag_clear_explicit(object, order)                         \
    __atomic_clear((bool *)(&((object)->value)), order)
#define atomic_flag_clear(object) \
    atomic_flag_clear_explicit(object, __ATOMIC_SEQ_CST)

/* Generic routines */
#define atomic_init(object, desired)                                      \
    atomic_store_explicit(object, desired, __ATOMIC_RELAXED)

#define atomic_store_explicit(object, desired, order)                     \
    ({ __typeof__ (object) ptr = (object);                                \
       __typeof__ (*ptr) tmp = (desired);                                 \
       __atomic_store (ptr, &tmp, (order));                               \
    })
#define atomic_store(object, desired)                                     \
     atomic_store_explicit (object, desired, __ATOMIC_SEQ_CST)

#define atomic_load_explicit(object, order)                               \
    ({ __typeof__ (object) ptr = (object);                                \
       __typeof__ (*ptr) tmp;                                             \
       __atomic_load (ptr, &tmp, (order));                                \
       tmp;                                                               \
    })
#define atomic_load(object) atomic_load_explicit (object, __ATOMIC_SEQ_CST)

#define atomic_exchange_explicit(object, desired, order)                  \
    ({ __typeof__ (object) ptr = (object);                                \
       __typeof__ (*ptr) val = (desired);                                 \
       __typeof__ (*ptr) tmp;                                             \
       __atomic_exchange (ptr, &val, &tmp, (order));                      \
       tmp;                                                               \
    })
#define atomic_exchange(object, desired)                                  \
  atomic_exchange_explicit (object, desired, __ATOMIC_SEQ_CST)

#define atomic_compare_exchange_strong_explicit(object, expected, desired, success, failure) \
    ({ __typeof__ (object) ptr = (object);                                \
       __typeof__ (*ptr) tmp = desired;                                   \
       __atomic_compare_exchange(ptr, expected, &tmp, 0, success, failure); \
    })
#define atomic_compare_exchange_strong(object, expected, desired)         \
    atomic_compare_exchange_strong_explicit (object, expected, desired,   \
                                             __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#define atomic_compare_exchange_weak_explicit(object, expected, desired, success, failure) \
    ({ __typeof__ (object) ptr = (object);                                \
       __typeof__ (*ptr) tmp = desired;                                   \
       __atomic_compare_exchange(ptr, expected, &tmp, 1, success, failure); \
    })
#define atomic_compare_exchange_weak(object, expected, desired)           \
    atomic_compare_exchange_weak_explicit (object, expected, desired,     \
                                           __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#define atomic_fetch_add(object, operand) \
    __atomic_fetch_add(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_add_explicit __atomic_fetch_add

#define atomic_fetch_sub(object, operand) \
    __atomic_fetch_sub(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit __atomic_fetch_sub

#define atomic_fetch_or(object, operand) \
    __atomic_fetch_or(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_or_explicit __atomic_fetch_or

#define atomic_fetch_xor(object, operand) \
    __atomic_fetch_xor(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_xor_explicit __atomic_fetch_xor

#define atomic_fetch_and(object, operand) \
    __atomic_fetch_and(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_and_explicit __atomic_fetch_and

extern void atomic_thread_fence (memory_order);
extern void __atomic_thread_fence (memory_order);
#define atomic_thread_fence(order) __atomic_thread_fence (order)
extern void atomic_signal_fence (memory_order);
extern void __atomic_signal_fence (memory_order);
#define atomic_signal_fence(order) __atomic_signal_fence  (order)
extern bool __atomic_is_lock_free(size_t size, void *ptr);
#define atomic_is_lock_free(OBJ) __atomic_is_lock_free (sizeof (*(OBJ)), (OBJ))

extern bool __atomic_test_and_set (void *, memory_order);
extern void __atomic_clear (bool *, memory_order);

#endif /* _STDATOMIC_H */
#ifndef _STDBOOL_H
#define _STDBOOL_H

/* ISOC99 boolean */

#define bool	_Bool
#define true	1
#define false	0
#define __bool_true_false_are_defined 1

#endif /* _STDBOOL_H */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ssize_t;
typedef __WCHAR_TYPE__ wchar_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __PTRDIFF_TYPE__ intptr_t;
typedef __SIZE_TYPE__ uintptr_t;

#if __STDC_VERSION__ >= 201112L
typedef union { long long __ll; long double __ld; } max_align_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#undef offsetof
#define offsetof(type, field) ((size_t)&((type *)0)->field)

#if defined __i386__ || defined __x86_64__
void *alloca(size_t size);
#endif

#endif

/* Older glibc require a wint_t from <stddef.h> (when requested
   by __need_wint_t, as otherwise stddef.h isn't allowed to
   define this type).   Note that this must be outside the normal
   _STDDEF_H guard, so that it works even when we've included the file
   already (without requiring wint_t).  Some other libs define _WINT_T
   if they've already provided that type, so we can use that as guard.
   TCC defines __WINT_TYPE__ for us.  */
#if defined (__need_wint_t)
#ifndef _WINT_T
#define _WINT_T
typedef __WINT_TYPE__ wint_t;
#endif
#undef __need_wint_t
#endif
#ifndef _STDNORETURN_H
#define _STDNORETURN_H

/* ISOC11 noreturn */
#define noreturn _Noreturn

#endif /* _STDNORETURN_H */
/*
 * ISO C Standard:  7.22  Type-generic math <tgmath.h>
 */

#ifndef _TGMATH_H
#define _TGMATH_H

#include <math.h>

#ifndef __cplusplus
#define __tgmath_real(x, F) \
  _Generic ((x), float: F##f, long double: F##l, default: F)(x)
#define __tgmath_real_2_1(x, y, F) \
  _Generic ((x), float: F##f, long double: F##l, default: F)(x, y)
#define __tgmath_real_2(x, y, F) \
  _Generic ((x)+(y), float: F##f, long double: F##l, default: F)(x, y)
#define __tgmath_real_3_2(x, y, z, F) \
  _Generic ((x)+(y), float: F##f, long double: F##l, default: F)(x, y, z)
#define __tgmath_real_3(x, y, z, F) \
  _Generic ((x)+(y)+(z), float: F##f, long double: F##l, default: F)(x, y, z)

/* Functions defined in both <math.h> and <complex.h> (7.22p4) */
#define acos(z)          __tgmath_real(z, acos)
#define asin(z)          __tgmath_real(z, asin)
#define atan(z)          __tgmath_real(z, atan)
#define acosh(z)         __tgmath_real(z, acosh)
#define asinh(z)         __tgmath_real(z, asinh)
#define atanh(z)         __tgmath_real(z, atanh)
#define cos(z)           __tgmath_real(z, cos)
#define sin(z)           __tgmath_real(z, sin)
#define tan(z)           __tgmath_real(z, tan)
#define cosh(z)          __tgmath_real(z, cosh)
#define sinh(z)          __tgmath_real(z, sinh)
#define tanh(z)          __tgmath_real(z, tanh)
#define exp(z)           __tgmath_real(z, exp)
#define log(z)           __tgmath_real(z, log)
#define pow(z1,z2)       __tgmath_real_2(z1, z2, pow)
#define sqrt(z)          __tgmath_real(z, sqrt)
#define fabs(z)          __tgmath_real(z, fabs)

/* Functions defined in <math.h> only (7.22p5) */
#define atan2(x,y)       __tgmath_real_2(x, y, atan2)
#define cbrt(x)          __tgmath_real(x, cbrt)
#define ceil(x)          __tgmath_real(x, ceil)
#define copysign(x,y)    __tgmath_real_2(x, y, copysign)
#define erf(x)           __tgmath_real(x, erf)
#define erfc(x)          __tgmath_real(x, erfc)
#define exp2(x)          __tgmath_real(x, exp2)
#define expm1(x)         __tgmath_real(x, expm1)
#define fdim(x,y)        __tgmath_real_2(x, y, fdim)
#define floor(x)         __tgmath_real(x, floor)
#define fma(x,y,z)       __tgmath_real_3(x, y, z, fma)
#define fmax(x,y)        __tgmath_real_2(x, y, fmax)
#define fmin(x,y)        __tgmath_real_2(x, y, fmin)
#define fmod(x,y)        __tgmath_real_2(x, y, fmod)
#define frexp(x,y)       __tgmath_real_2_1(x, y, frexp)
#define hypot(x,y)       __tgmath_real_2(x, y, hypot)
#define ilogb(x)         __tgmath_real(x, ilogb)
#define ldexp(x,y)       __tgmath_real_2_1(x, y, ldexp)
#define lgamma(x)        __tgmath_real(x, lgamma)
#define llrint(x)        __tgmath_real(x, llrint)
#define llround(x)       __tgmath_real(x, llround)
#define log10(x)         __tgmath_real(x, log10)
#define log1p(x)         __tgmath_real(x, log1p)
#define log2(x)          __tgmath_real(x, log2)
#define logb(x)          __tgmath_real(x, logb)
#define lrint(x)         __tgmath_real(x, lrint)
#define lround(x)        __tgmath_real(x, lround)
#define nearbyint(x)     __tgmath_real(x, nearbyint)
#define nextafter(x,y)   __tgmath_real_2(x, y, nextafter)
#define nexttoward(x,y)  __tgmath_real_2(x, y, nexttoward)
#define remainder(x,y)   __tgmath_real_2(x, y, remainder)
#define remquo(x,y,z)    __tgmath_real_3_2(x, y, z, remquo)
#define rint(x)          __tgmath_real(x, rint)
#define round(x)         __tgmath_real(x, round)
#define scalbln(x,y)     __tgmath_real_2_1(x, y, scalbln)
#define scalbn(x,y)      __tgmath_real_2_1(x, y, scalbn)
#define tgamma(x)        __tgmath_real(x, tgamma)
#define trunc(x)         __tgmath_real(x, trunc)

/* Functions defined in <complex.h> only (7.22p6)
#define carg(z)          __tgmath_cplx_only(z, carg)
#define cimag(z)         __tgmath_cplx_only(z, cimag)
#define conj(z)          __tgmath_cplx_only(z, conj)
#define cproj(z)         __tgmath_cplx_only(z, cproj)
#define creal(z)         __tgmath_cplx_only(z, creal)
*/
#endif /* __cplusplus */
#endif /* _TGMATH_H */
/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER within this package.
 */
