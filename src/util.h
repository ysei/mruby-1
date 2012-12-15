//
//  util.h
//  mruby
//
//  Created by Christian Mauceri on 14/12/12.
//
//

#ifndef mruby_util_h
#define mruby_util_h
#include <string.h>
#ifdef __LP64__
#define SIZEOF_LONG 8
#else
#define SIZEOF_LONG 4
#endif

#ifdef __STDC__
#include <limits.h>
#else
#ifndef LONG_MAX
#ifdef HAVE_LIMITS_H
#include <limits.h>
#else
/* assuming 32bit(2's compliment) long */
#define LONG_MAX 2147483647
#endif
#endif
#ifndef LONG_MIN
#define LONG_MIN (-LONG_MAX-1)
#endif
#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif
#endif
unsigned long ruby_strtoul(const char *str, char **endptr, int base);
#define xmalloc(l) malloc((l))
#define xfree(b) free((b))

#define VALUE unsigned long
#define SIGNED_VALUE intptr_t
#define INT2FIX(i) ((VALUE)(((SIGNED_VALUE)(i))<<1 | FIXNUM_FLAG))
#define SIZEOF_INT   4
#define SIZEOF_BDIGITS SIZEOF_INT
#define BDIGIT unsigned int
#define BDIGIT_DBL unsigned long
#define FIXNUM_MAX (LONG_MAX>>1)
#define FIXNUM_MIN ((long)LONG_MIN >> 1)

#define FIX2LONG(x) (long)((SIGNED_VALUE)(x) >> 1)
#define FIX2ULONG(x) ((((VALUE)(x))>>1)&LONG_MAX)
#define FIXABLE(f) (POSFIXABLE(f) && NEGFIXABLE(f))
#define INT2FIX(i) ((VALUE)(((SIGNED_VALUE)(i))<<1 | FIXNUM_FLAG))
#define LONG2FIX(i) INT2FIX(i)
VALUE mrb_dbl_cmp(double a, double b);
mrb_value mrb_float_new(mrb_state *mrb, double d);


#define MEMZERO(p,type,n) memset((p), 0, sizeof(type)*(n))
#define MEMCPY(p1,p2,type,n) memcpy((p1), (p2), sizeof(type)*(n))
#define MEMMOVE(p1,p2,type,n) memmove((p1), (p2), sizeof(type)*(n))
#define MEMCMP(p1,p2,type,n) memcmp((p1), (p2), sizeof(type)*(n))
#define STRTOUL(str, endptr, base) (ruby_strtoul((str), (endptr), (base)))

/*
enum ruby_special_consts {
    RUBY_Qfalse = 0,
    RUBY_Qtrue  = 2,
    RUBY_Qnil   = 4,
    RUBY_Qundef = 6,
    RUBY_IMMEDIATE_MASK = 0x03,
    RUBY_FIXNUM_FLAG    = 0x01,
    RUBY_SYMBOL_FLAG    = 0x0e,
    RUBY_SPECIAL_SHIFT  = 8
};

*/
#define IMMEDIATE_MASK 0x03
#define FIXNUM_FLAG 0x01
#define SYMBOL_FLAG 0x0e
/*
#define ALLOC_N(mrb,type,n) ((type*)malloc((n)*sizeof(type)))
#define ALLOC(type) ((type*)xmalloc(sizeof(type)))
#define REALLOC_N(var,type,n) ((var)=(type*)realloc((char*)(var),(n)*sizeof(type)))

#define ALLOCA_N(type,n) ((type*)alloca(sizeof(type)*(n)))
#ifdef C_ALLOCA
# define ALLOCV(v, n) rb_alloc_tmp_buffer(&(v), (n))
#else
# define ALLOCV(v, n) ((n) < 1024 ? (RB_GC_GUARD(v) = 0, alloca(n)) : rb_alloc_tmp_buffer(&(v), (n)))
#endif
#define ALLOCV_N(type, v, n) ((type*)ALLOCV((v), sizeof(type)*(n)))
#define ALLOCV_END(v) rb_free_tmp_buffer(&(v))
VALUE mrb_dbl_cmp(double a, double b);
*/

#endif
