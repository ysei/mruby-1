/**********************************************************************

  util.c -

  $Author: usa $
  created at: Fri Mar 10 17:22:34 JST 1995

  Copyright (C) 1993-2008 Yukihiro Matsumoto

**********************************************************************/

#include "mruby.h"
#include "util.h"
//#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <float.h>

#ifdef _WIN32
#include "missing/file.h"
#endif

#include "ruby/util.h"




static unsigned long
scan_digits(const char *str, int base, size_t *retlen, int *overflow)
{
    static signed char table[] = {
        /*     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
        /*0*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*1*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*2*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*3*/  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
        /*4*/ -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
        /*5*/ 25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
        /*6*/ -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
        /*7*/ 25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
        /*8*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*9*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*a*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*b*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*c*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*d*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*e*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        /*f*/ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };

    const char *start = str;
    unsigned long ret = 0, x;
    unsigned long mul_overflow = (~(unsigned long)0) / base;
    int c;
    *overflow = 0;

    while ((c = (unsigned char)*str++) != '\0') {
        int d = table[c];
        if (d == -1 || base <= d) {
            *retlen = (str-1) - start;
            return ret;
        }
        if (mul_overflow < ret)
            *overflow = 1;
        ret *= base;
        x = ret;
        ret += d;
        if (ret < x)
            *overflow = 1;
    }
    *retlen = (str-1) - start;
    return ret;
}

unsigned long
ruby_strtoul(const char *str, char **endptr, int base)
{
    int c, b, overflow;
    int sign = 0;
    size_t len;
    unsigned long ret;
    const char *subject_found = str;

    if (base == 1 || 36 < base) {
        errno = EINVAL;
        return 0;
    }

    while ((c = *str) && ISSPACE(c))
        str++;

    if (c == '+') {
        sign = 1;
        str++;
    }
    else if (c == '-') {
        sign = -1;
        str++;
    }

    if (str[0] == '0') {
        subject_found = str+1;
        if (base == 0 || base == 16) {
            if (str[1] == 'x' || str[1] == 'X') {
                b = 16;
                str += 2;
            }
            else {
                b = base == 0 ? 8 : 16;
                str++;
            }
        }
        else {
            b = base;
            str++;
        }
    }
    else {
        b = base == 0 ? 10 : base;
    }

    ret = scan_digits(str, b, &len, &overflow);

    if (0 < len)
        subject_found = str+len;

    if (endptr)
        *endptr = (char*)subject_found;

    if (overflow) {
        errno = ERANGE;
        return ULONG_MAX;
    }

    if (sign < 0) {
        ret = (unsigned long)(-(long)ret);
        return ret;
    }
    else {
        return ret;
    }
}

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif

#ifndef S_ISDIR
#   define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif


/* mm.c */

#define A ((int*)a)
#define B ((int*)b)
#define C ((int*)c)
#define D ((int*)d)

#define mmprepare(base, size) do {\
 if (((VALUE)(base) & (0x3)) == 0)\
   if ((size) >= 16) mmkind = 1;\
   else              mmkind = 0;\
 else                mmkind = -1;\
 high = ((size) & (~0xf));\
 low  = ((size) &  0x0c);\
} while (0)\

#define mmarg mmkind, size, high, low

static void mmswap_(register char *a, register char *b, int mmkind, size_t size, size_t high, size_t low)
{
 register int s;
 if (a == b) return;
 if (mmkind >= 0) {
   if (mmkind > 0) {
     register char *t = a + high;
     do {
       s = A[0]; A[0] = B[0]; B[0] = s;
       s = A[1]; A[1] = B[1]; B[1] = s;
       s = A[2]; A[2] = B[2]; B[2] = s;
       s = A[3]; A[3] = B[3]; B[3] = s;  a += 16; b += 16;
     } while (a < t);
   }
   if (low != 0) { s = A[0]; A[0] = B[0]; B[0] = s;
     if (low >= 8) { s = A[1]; A[1] = B[1]; B[1] = s;
       if (low == 12) {s = A[2]; A[2] = B[2]; B[2] = s;}}}
 }
 else {
   register char *t = a + size;
   do {s = *a; *a++ = *b; *b++ = s;} while (a < t);
 }
}
#define mmswap(a,b) mmswap_((a),(b),mmarg)

static void mmrot3_(register char *a, register char *b, register char *c, int mmkind, size_t size, size_t high, size_t low)
{
 register int s;
 if (mmkind >= 0) {
   if (mmkind > 0) {
     register char *t = a + high;
     do {
       s = A[0]; A[0] = B[0]; B[0] = C[0]; C[0] = s;
       s = A[1]; A[1] = B[1]; B[1] = C[1]; C[1] = s;
       s = A[2]; A[2] = B[2]; B[2] = C[2]; C[2] = s;
       s = A[3]; A[3] = B[3]; B[3] = C[3]; C[3] = s; a += 16; b += 16; c += 16;
     } while (a < t);
   }
   if (low != 0) { s = A[0]; A[0] = B[0]; B[0] = C[0]; C[0] = s;
     if (low >= 8) { s = A[1]; A[1] = B[1]; B[1] = C[1]; C[1] = s;
       if (low == 12) {s = A[2]; A[2] = B[2]; B[2] = C[2]; C[2] = s;}}}
 }
 else {
   register char *t = a + size;
   do {s = *a; *a++ = *b; *b++ = *c; *c++ = s;} while (a < t);
 }
}
#define mmrot3(a,b,c) mmrot3_((a),(b),(c),mmarg)

/* qs6.c */
/*****************************************************/
/*                                                   */
/*          qs6   (Quick sort function)              */
/*                                                   */
/* by  Tomoyuki Kawamura              1995.4.21      */
/* kawamura@tokuyama.ac.jp                           */
/*****************************************************/

typedef struct { char *LL, *RR; } stack_node; /* Stack structure for L,l,R,r */
#define PUSH(ll,rr) do { top->LL = (ll); top->RR = (rr); ++top; } while (0)  /* Push L,l,R,r */
#define POP(ll,rr)  do { --top; (ll) = top->LL; (rr) = top->RR; } while (0)      /* Pop L,l,R,r */

#define med3(a,b,c) ((*cmp)((a),(b),d)<0 ?                                   \
                       ((*cmp)((b),(c),d)<0 ? (b) : ((*cmp)((a),(c),d)<0 ? (c) : (a))) : \
                       ((*cmp)((b),(c),d)>0 ? (b) : ((*cmp)((a),(c),d)<0 ? (a) : (c))))

void
ruby_qsort(void* base, const size_t nel, const size_t size,
	   int (*cmp)(const void*, const void*, void*), void *d)
{
  register char *l, *r, *m;          	/* l,r:left,right group   m:median point */
  register int t, eq_l, eq_r;       	/* eq_l: all items in left group are equal to S */
  char *L = base;                    	/* left end of current region */
  char *R = (char*)base + size*(nel-1); /* right end of current region */
  size_t chklim = 63;                   /* threshold of ordering element check */
  stack_node stack[32], *top = stack;   /* 32 is enough for 32bit CPU */
  int mmkind;
  size_t high, low, n;

  if (nel <= 1) return;        /* need not to sort */
  mmprepare(base, size);
  goto start;

  nxt:
  if (stack == top) return;    /* return if stack is empty */
  POP(L,R);

  for (;;) {
    start:
    if (L + size == R) {       /* 2 elements */
      if ((*cmp)(L,R,d) > 0) mmswap(L,R); goto nxt;
    }

    l = L; r = R;
    n = (r - l + size) / size;  /* number of elements */
    m = l + size * (n >> 1);    /* calculate median value */

    if (n >= 60) {
      register char *m1;
      register char *m3;
      if (n >= 200) {
	n = size*(n>>3); /* number of bytes in splitting 8 */
	{
	  register char *p1 = l  + n;
	  register char *p2 = p1 + n;
	  register char *p3 = p2 + n;
	  m1 = med3(p1, p2, p3);
	  p1 = m  + n;
	  p2 = p1 + n;
	  p3 = p2 + n;
	  m3 = med3(p1, p2, p3);
	}
      }
      else {
	n = size*(n>>2); /* number of bytes in splitting 4 */
	m1 = l + n;
	m3 = m + n;
      }
      m = med3(m1, m, m3);
    }

    if ((t = (*cmp)(l,m,d)) < 0) {                           /*3-5-?*/
      if ((t = (*cmp)(m,r,d)) < 0) {                         /*3-5-7*/
	if (chklim && nel >= chklim) {   /* check if already ascending order */
	  char *p;
	  chklim = 0;
	  for (p=l; p<r; p+=size) if ((*cmp)(p,p+size,d) > 0) goto fail;
	  goto nxt;
	}
	fail: goto loopA;                                    /*3-5-7*/
      }
      if (t > 0) {
	if ((*cmp)(l,r,d) <= 0) {mmswap(m,r); goto loopA;}     /*3-5-4*/
	mmrot3(r,m,l); goto loopA;                           /*3-5-2*/
      }
      goto loopB;                                            /*3-5-5*/
    }

    if (t > 0) {                                             /*7-5-?*/
      if ((t = (*cmp)(m,r,d)) > 0) {                         /*7-5-3*/
	if (chklim && nel >= chklim) {   /* check if already ascending order */
	  char *p;
	  chklim = 0;
	  for (p=l; p<r; p+=size) if ((*cmp)(p,p+size,d) < 0) goto fail2;
	  while (l<r) {mmswap(l,r); l+=size; r-=size;}  /* reverse region */
	  goto nxt;
	}
	fail2: mmswap(l,r); goto loopA;                      /*7-5-3*/
      }
      if (t < 0) {
	if ((*cmp)(l,r,d) <= 0) {mmswap(l,m); goto loopB;}   /*7-5-8*/
	mmrot3(l,m,r); goto loopA;                           /*7-5-6*/
      }
      mmswap(l,r); goto loopA;                               /*7-5-5*/
    }

    if ((t = (*cmp)(m,r,d)) < 0)  {goto loopA;}              /*5-5-7*/
    if (t > 0) {mmswap(l,r); goto loopB;}                    /*5-5-3*/

    /* determining splitting type in case 5-5-5 */           /*5-5-5*/
    for (;;) {
      if ((l += size) == r)      goto nxt;                   /*5-5-5*/
      if (l == m) continue;
      if ((t = (*cmp)(l,m,d)) > 0) {mmswap(l,r); l = L; goto loopA;}/*575-5*/
      if (t < 0)                 {mmswap(L,l); l = L; goto loopB;}  /*535-5*/
    }

    loopA: eq_l = 1; eq_r = 1;  /* splitting type A */ /* left <= median < right */
    for (;;) {
      for (;;) {
	if ((l += size) == r)
	  {l -= size; if (l != m) mmswap(m,l); l -= size; goto fin;}
	if (l == m) continue;
	if ((t = (*cmp)(l,m,d)) > 0) {eq_r = 0; break;}
	if (t < 0) eq_l = 0;
      }
      for (;;) {
	if (l == (r -= size))
	  {l -= size; if (l != m) mmswap(m,l); l -= size; goto fin;}
	if (r == m) {m = l; break;}
	if ((t = (*cmp)(r,m,d)) < 0) {eq_l = 0; break;}
	if (t == 0) break;
      }
      mmswap(l,r);    /* swap left and right */
    }

    loopB: eq_l = 1; eq_r = 1;  /* splitting type B */ /* left < median <= right */
    for (;;) {
      for (;;) {
	if (l == (r -= size))
	  {r += size; if (r != m) mmswap(r,m); r += size; goto fin;}
	if (r == m) continue;
	if ((t = (*cmp)(r,m,d)) < 0) {eq_l = 0; break;}
	if (t > 0) eq_r = 0;
      }
      for (;;) {
	if ((l += size) == r)
	  {r += size; if (r != m) mmswap(r,m); r += size; goto fin;}
	if (l == m) {m = r; break;}
	if ((t = (*cmp)(l,m,d)) > 0) {eq_r = 0; break;}
	if (t == 0) break;
      }
      mmswap(l,r);    /* swap left and right */
    }

    fin:
    if (eq_l == 0)                         /* need to sort left side */
      if (eq_r == 0)                       /* need to sort right side */
	if (l-L < R-r) {PUSH(r,R); R = l;} /* sort left side first */
	else           {PUSH(L,l); L = r;} /* sort right side first */
      else R = l;                          /* need to sort left side only */
    else if (eq_r == 0) L = r;             /* need to sort right side only */
    else goto nxt;                         /* need not to sort both sides */
  }
}

char *
ruby_strdup(const char *str)
{
    char *tmp;
    size_t len = strlen(str) + 1;

    tmp = xmalloc(len);
    memcpy(tmp, str, len);

    return tmp;
}

char *
ruby_getcwd(void)
{
#ifdef HAVE_GETCWD
    int size = 200;
    char *buf = xmalloc(size);

    while (!getcwd(buf, size)) {
	if (errno != ERANGE) {
	    xfree(buf);
	    rb_sys_fail("getcwd");
	}
	size *= 2;
	buf = xrealloc(buf, size);
    }
#else
# ifndef PATH_MAX
#  define PATH_MAX 8192
# endif
    char *buf = xmalloc(PATH_MAX+1);

    if (!getwd(buf)) {
	xfree(buf);
	//mrb_raise("getwd");
    }
#endif
    return buf;
}

/****************************************************************
 *
 * The author of this software is David M. Gay.
 *
 * Copyright (c) 1991, 2000, 2001 by Lucent Technologies.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose without fee is hereby granted, provided that this entire notice
 * is included in all copies of any software which is or includes a copy
 * or modification of this software and in all copies of the supporting
 * documentation for such software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTY.  IN PARTICULAR, NEITHER THE AUTHOR NOR LUCENT MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE MERCHANTABILITY
 * OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 ***************************************************************/

/* Please send bug reports to David M. Gay (dmg at acm dot org,
 * with " at " changed at "@" and " dot " changed to ".").	*/

/* On a machine with IEEE extended-precision registers, it is
 * necessary to specify double-precision (53-bit) rounding precision
 * before invoking strtod or dtoa.  If the machine uses (the equivalent
 * of) Intel 80x87 arithmetic, the call
 *	_control87(PC_53, MCW_PC);
 * does this with many compilers.  Whether this or another call is
 * appropriate depends on the compiler; for this to work, it may be
 * necessary to #include "float.h" or another system-dependent header
 * file.
 */

/* strtod for IEEE-, VAX-, and IBM-arithmetic machines.
 *
 * This strtod returns a nearest machine number to the input decimal
 * string (or sets errno to ERANGE).  With IEEE arithmetic, ties are
 * broken by the IEEE round-even rule.  Otherwise ties are broken by
 * biased rounding (add half and chop).
 *
 * Inspired loosely by William D. Clinger's paper "How to Read Floating
 * Point Numbers Accurately" [Proc. ACM SIGPLAN '90, pp. 92-101].
 *
 * Modifications:
 *
 *	1. We only require IEEE, IBM, or VAX double-precision
 *		arithmetic (not IEEE double-extended).
 *	2. We get by with floating-point arithmetic in a case that
 *		Clinger missed -- when we're computing d * 10^n
 *		for a small integer d and the integer n is not too
 *		much larger than 22 (the maximum integer k for which
 *		we can represent 10^k exactly), we may be able to
 *		compute (d*10^k) * 10^(e-k) with just one roundoff.
 *	3. Rather than a bit-at-a-time adjustment of the binary
 *		result in the hard case, we use floating-point
 *		arithmetic to determine the adjustment to within
 *		one bit; only in really hard cases do we need to
 *		compute a second residual.
 *	4. Because of 3., we don't need a large table of powers of 10
 *		for ten-to-e (just some small tables, e.g. of 10^k
 *		for 0 <= k <= 22).
 */

/*
 * #define IEEE_LITTLE_ENDIAN for IEEE-arithmetic machines where the least
 *	significant byte has the lowest address.
 * #define IEEE_BIG_ENDIAN for IEEE-arithmetic machines where the most
 *	significant byte has the lowest address.
 * #define Long int on machines with 32-bit ints and 64-bit longs.
 * #define IBM for IBM mainframe-style floating-point arithmetic.
 * #define VAX for VAX-style floating-point arithmetic (D_floating).
 * #define No_leftright to omit left-right logic in fast floating-point
 *	computation of dtoa.
 * #define Honor_FLT_ROUNDS if FLT_ROUNDS can assume the values 2 or 3
 *	and strtod and dtoa should round accordingly.
 * #define Check_FLT_ROUNDS if FLT_ROUNDS can assume the values 2 or 3
 *	and Honor_FLT_ROUNDS is not #defined.
 * #define RND_PRODQUOT to use rnd_prod and rnd_quot (assembly routines
 *	that use extended-precision instructions to compute rounded
 *	products and quotients) with IBM.
 * #define ROUND_BIASED for IEEE-format with biased rounding.
 * #define Inaccurate_Divide for IEEE-format with correctly rounded
 *	products but inaccurate quotients, e.g., for Intel i860.
 * #define NO_LONG_LONG on machines that do not have a "long long"
 *	integer type (of >= 64 bits).  On such machines, you can
 *	#define Just_16 to store 16 bits per 32-bit Long when doing
 *	high-precision integer arithmetic.  Whether this speeds things
 *	up or slows things down depends on the machine and the number
 *	being converted.  If long long is available and the name is
 *	something other than "long long", #define Llong to be the name,
 *	and if "unsigned Llong" does not work as an unsigned version of
 *	Llong, #define #ULLong to be the corresponding unsigned type.
 * #define KR_headers for old-style C function headers.
 * #define Bad_float_h if your system lacks a float.h or if it does not
 *	define some or all of DBL_DIG, DBL_MAX_10_EXP, DBL_MAX_EXP,
 *	FLT_RADIX, FLT_ROUNDS, and DBL_MAX.
 * #define MALLOC your_malloc, where your_malloc(n) acts like malloc(n)
 *	if memory is available and otherwise does something you deem
 *	appropriate.  If MALLOC is undefined, malloc will be invoked
 *	directly -- and assumed always to succeed.
 * #define Omit_Private_Memory to omit logic (added Jan. 1998) for making
 *	memory allocations from a private pool of memory when possible.
 *	When used, the private pool is PRIVATE_MEM bytes long:  2304 bytes,
 *	unless #defined to be a different length.  This default length
 *	suffices to get rid of MALLOC calls except for unusual cases,
 *	such as decimal-to-binary conversion of a very long string of
 *	digits.  The longest string dtoa can return is about 751 bytes
 *	long.  For conversions by strtod of strings of 800 digits and
 *	all dtoa conversions in single-threaded executions with 8-byte
 *	pointers, PRIVATE_MEM >= 7400 appears to suffice; with 4-byte
 *	pointers, PRIVATE_MEM >= 7112 appears adequate.
 * #define INFNAN_CHECK on IEEE systems to cause strtod to check for
 *	Infinity and NaN (case insensitively).  On some systems (e.g.,
 *	some HP systems), it may be necessary to #define NAN_WORD0
 *	appropriately -- to the most significant word of a quiet NaN.
 *	(On HP Series 700/800 machines, -DNAN_WORD0=0x7ff40000 works.)
 *	When INFNAN_CHECK is #defined and No_Hex_NaN is not #defined,
 *	strtod also accepts (case insensitively) strings of the form
 *	NaN(x), where x is a string of hexadecimal digits and spaces;
 *	if there is only one string of hexadecimal digits, it is taken
 *	for the 52 fraction bits of the resulting NaN; if there are two
 *	or more strings of hex digits, the first is for the high 20 bits,
 *	the second and subsequent for the low 32 bits, with intervening
 *	white space ignored; but if this results in none of the 52
 *	fraction bits being on (an IEEE Infinity symbol), then NAN_WORD0
 *	and NAN_WORD1 are used instead.
 * #define MULTIPLE_THREADS if the system offers preemptively scheduled
 *	multiple threads.  In this case, you must provide (or suitably
 *	#define) two locks, acquired by ACQUIRE_DTOA_LOCK(n) and freed
 *	by FREE_DTOA_LOCK(n) for n = 0 or 1.  (The second lock, accessed
 *	in pow5mult, ensures lazy evaluation of only one copy of high
 *	powers of 5; omitting this lock would introduce a small
 *	probability of wasting memory, but would otherwise be harmless.)
 *	You must also invoke freedtoa(s) to free the value s returned by
 *	dtoa.  You may do so whether or not MULTIPLE_THREADS is #defined.
 * #define NO_IEEE_Scale to disable new (Feb. 1997) logic in strtod that
 *	avoids underflows on inputs whose result does not underflow.
 *	If you #define NO_IEEE_Scale on a machine that uses IEEE-format
 *	floating-point numbers and flushes underflows to zero rather
 *	than implementing gradual underflow, then you must also #define
 *	Sudden_Underflow.
 * #define YES_ALIAS to permit aliasing certain double values with
 *	arrays of ULongs.  This leads to slightly better code with
 *	some compilers and was always used prior to 19990916, but it
 *	is not strictly legal and can cause trouble with aggressively
 *	optimizing compilers (e.g., gcc 2.95.1 under -O2).
 * #define USE_LOCALE to use the current locale's decimal_point value.
 * #define SET_INEXACT if IEEE arithmetic is being used and extra
 *	computation should be done to set the inexact flag when the
 *	result is inexact and avoid setting inexact when the result
 *	is exact.  In this case, dtoa.c must be compiled in
 *	an environment, perhaps provided by #include "dtoa.c" in a
 *	suitable wrapper, that defines two functions,
 *		int get_inexact(void);
 *		void clear_inexact(void);
 *	such that get_inexact() returns a nonzero value if the
 *	inexact bit is already set, and clear_inexact() sets the
 *	inexact bit to 0.  When SET_INEXACT is #defined, strtod
 *	also does extra computations to set the underflow and overflow
 *	flags when appropriate (i.e., when the result is tiny and
 *	inexact or when it is a numeric value rounded to +-infinity).
 * #define NO_ERRNO if strtod should not assign errno = ERANGE when
 *	the result overflows to +-Infinity or underflows to 0.
 */

#ifdef WORDS_BIGENDIAN
#define IEEE_BIG_ENDIAN
#else
#define IEEE_LITTLE_ENDIAN
#endif

#ifdef __vax__
#define VAX
#undef IEEE_BIG_ENDIAN
#undef IEEE_LITTLE_ENDIAN
#endif

#if defined(__arm__) && !defined(__VFP_FP__)
#define IEEE_BIG_ENDIAN
#undef IEEE_LITTLE_ENDIAN
#endif

#undef Long
#undef ULong

#if SIZEOF_INT == 4
#define Long int
#define ULong unsigned int
#elif SIZEOF_LONG == 4
#define Long long int
#define ULong unsigned long int
#endif

#if HAVE_LONG_LONG
#define Llong LONG_LONG
#endif

#ifdef DEBUG
#include "stdio.h"
#define Bug(x) {fprintf(stderr, "%s\n", (x)); exit(EXIT_FAILURE);}
#endif

#include "stdlib.h"
#include "string.h"

#ifdef USE_LOCALE
#include "locale.h"
#endif

#ifdef MALLOC
extern void *MALLOC(size_t);
#else
#define MALLOC malloc
#endif

#ifndef Omit_Private_Memory
#ifndef PRIVATE_MEM
#define PRIVATE_MEM 2304
#endif
#define PRIVATE_mem ((PRIVATE_MEM+sizeof(double)-1)/sizeof(double))\
static double private_mem[PRIVATE_mem], *pmem_next = private_mem;
#endif

#undef IEEE_Arith
#undef Avoid_Underflow
#ifdef IEEE_BIG_ENDIAN
#define IEEE_Arith
#endif
#ifdef IEEE_LITTLE_ENDIAN
#define IEEE_Arith
#endif

#ifdef Bad_float_h

#ifdef IEEE_Arith
#define DBL_DIG 15
#define DBL_MAX_10_EXP 308
#define DBL_MAX_EXP 1024
#define FLT_RADIX 2
#endif /*IEEE_Arith*/

#ifdef IBM
#define DBL_DIG 16
#define DBL_MAX_10_EXP 75
#define DBL_MAX_EXP 63
#define FLT_RADIX 16
#define DBL_MAX 7.2370055773322621e+75
#endif

#ifdef VAX
#define DBL_DIG 16
#define DBL_MAX_10_EXP 38
#define DBL_MAX_EXP 127
#define FLT_RADIX 2
#define DBL_MAX 1.7014118346046923e+38
#endif

#ifndef LONG_MAX
#define LONG_MAX 2147483647
#endif

#else /* ifndef Bad_float_h */
#include "float.h"
#endif /* Bad_float_h */

#ifndef __MATH_H__
#include "math.h"
#endif

#ifdef __cplusplus
extern "C" {
#if 0
}
#endif
#endif

#if defined(IEEE_LITTLE_ENDIAN) + defined(IEEE_BIG_ENDIAN) + defined(VAX) + defined(IBM) != 1
Exactly one of IEEE_LITTLE_ENDIAN, IEEE_BIG_ENDIAN, VAX, or IBM should be defined.
#endif

/*
typedef union { double d; ULong L[2]; } U;

#ifdef YES_ALIAS
typedef double double_u;
#  define dval(x) (x)
#  ifdef IEEE_LITTLE_ENDIAN
#    define word0(x) (((ULong *)&(x))[1])
#    define word1(x) (((ULong *)&(x))[0])
#  else
#    define word0(x) (((ULong *)&(x))[0])
#    define word1(x) (((ULong *)&(x))[1])
#  endif
#else
typedef U double_u;
#  ifdef IEEE_LITTLE_ENDIAN
#    define word0(x) ((x).L[1])
#    define word1(x) ((x).L[0])
#  else
#    define word0(x) ((x).L[0])
#    define word1(x) ((x).L[1])
#  endif
#  define dval(x) ((x).d)
#endif
*/

/* The following definition of Storeinc is appropriate for MIPS processors.
 * An alternative that might be better on some machines is
 * #define Storeinc(a,b,c) (*a++ = b << 16 | c & 0xffff)
 */
#if defined(IEEE_LITTLE_ENDIAN) + defined(VAX) + defined(__arm__)
#define Storeinc(a,b,c) (((unsigned short *)(a))[1] = (unsigned short)(b), \
((unsigned short *)(a))[0] = (unsigned short)(c), (a)++)
#else
#define Storeinc(a,b,c) (((unsigned short *)(a))[0] = (unsigned short)(b), \
((unsigned short *)(a))[1] = (unsigned short)(c), (a)++)
#endif

/* #define P DBL_MANT_DIG */
/* Ten_pmax = floor(P*log(2)/log(5)) */
/* Bletch = (highest power of 2 < DBL_MAX_10_EXP) / 16 */
/* Quick_max = floor((P-1)*log(FLT_RADIX)/log(10) - 1) */
/* Int_max = floor(P*log(FLT_RADIX)/log(10) - 1) */

#ifdef IEEE_Arith
#define Exp_shift  20
#define Exp_shift1 20
#define Exp_msk1    0x100000
#define Exp_msk11   0x100000
#define Exp_mask  0x7ff00000
#define P 53
#define Bias 1023
#define Emin (-1022)
#define Exp_1  0x3ff00000
#define Exp_11 0x3ff00000
#define Ebits 11
#define Frac_mask  0xfffff
#define Frac_mask1 0xfffff
#define Ten_pmax 22
#define Bletch 0x10
#define Bndry_mask  0xfffff
#define Bndry_mask1 0xfffff
#define LSB 1
#define Sign_bit 0x80000000
#define Log2P 1
#define Tiny0 0
#define Tiny1 1
#define Quick_max 14
#define Int_max 14
#ifndef NO_IEEE_Scale
#define Avoid_Underflow
#ifdef Flush_Denorm	/* debugging option */
#undef Sudden_Underflow
#endif
#endif

#ifndef Flt_Rounds
#ifdef FLT_ROUNDS
#define Flt_Rounds FLT_ROUNDS
#else
#define Flt_Rounds 1
#endif
#endif /*Flt_Rounds*/

#ifdef Honor_FLT_ROUNDS
#define Rounding rounding
#undef Check_FLT_ROUNDS
#define Check_FLT_ROUNDS
#else
#define Rounding Flt_Rounds
#endif

#else /* ifndef IEEE_Arith */
#undef Check_FLT_ROUNDS
#undef Honor_FLT_ROUNDS
#undef SET_INEXACT
#undef  Sudden_Underflow
#define Sudden_Underflow
#ifdef IBM
#undef Flt_Rounds
#define Flt_Rounds 0
#define Exp_shift  24
#define Exp_shift1 24
#define Exp_msk1   0x1000000
#define Exp_msk11  0x1000000
#define Exp_mask  0x7f000000
#define P 14
#define Bias 65
#define Exp_1  0x41000000
#define Exp_11 0x41000000
#define Ebits 8	/* exponent has 7 bits, but 8 is the right value in b2d */
#define Frac_mask  0xffffff
#define Frac_mask1 0xffffff
#define Bletch 4
#define Ten_pmax 22
#define Bndry_mask  0xefffff
#define Bndry_mask1 0xffffff
#define LSB 1
#define Sign_bit 0x80000000
#define Log2P 4
#define Tiny0 0x100000
#define Tiny1 0
#define Quick_max 14
#define Int_max 15
#else /* VAX */
#undef Flt_Rounds
#define Flt_Rounds 1
#define Exp_shift  23
#define Exp_shift1 7
#define Exp_msk1    0x80
#define Exp_msk11   0x800000
#define Exp_mask  0x7f80
#define P 56
#define Bias 129
#define Exp_1  0x40800000
#define Exp_11 0x4080
#define Ebits 8
#define Frac_mask  0x7fffff
#define Frac_mask1 0xffff007f
#define Ten_pmax 24
#define Bletch 2
#define Bndry_mask  0xffff007f
#define Bndry_mask1 0xffff007f
#define LSB 0x10000
#define Sign_bit 0x8000
#define Log2P 1
#define Tiny0 0x80
#define Tiny1 0
#define Quick_max 15
#define Int_max 15
#endif /* IBM, VAX */
#endif /* IEEE_Arith */

#ifndef IEEE_Arith
#define ROUND_BIASED
#endif

#ifdef RND_PRODQUOT
#define rounded_product(a,b) ((a) = rnd_prod((a), (b)))
#define rounded_quotient(a,b) ((a) = rnd_quot((a), (b)))
extern double rnd_prod(double, double), rnd_quot(double, double);
#else
#define rounded_product(a,b) ((a) *= (b))
#define rounded_quotient(a,b) ((a) /= (b))
#endif

#define Big0 (Frac_mask1 | Exp_msk1*(DBL_MAX_EXP+Bias-1))
#define Big1 0xffffffff

#ifndef Pack_32
#define Pack_32
#endif

#define FFFFFFFF 0xffffffffUL


#define Kmax 15


#ifndef MULTIPLE_THREADS
static char *dtoa_result;
#endif

#ifndef MULTIPLE_THREADS
static char *
rv_alloc(int i)
{
    return dtoa_result = xmalloc(i);
}
#else
#define rv_alloc(i) xmalloc(i)
#endif

static char *
nrv_alloc(const char *s, char **rve, size_t n)
{
    char *rv, *t;

    t = rv = rv_alloc(n);
    while ((*t = *s++) != 0) t++;
    if (rve)
        *rve = t;
    return rv;
}

#define rv_strdup(s, rve) nrv_alloc((s), (rve), strlen(s)+1)

static const char INFSTR[] = "Infinity";
static const char NANSTR[] = "NaN";
static const char ZEROSTR[] = "0";

/* dtoa for IEEE arithmetic (dmg): convert double to ASCII string.
 *
 * Inspired by "How to Print Floating-Point Numbers Accurately" by
 * Guy L. Steele, Jr. and Jon L. White [Proc. ACM SIGPLAN '90, pp. 112-126].
 *
 * Modifications:
 *  1. Rather than iterating, we use a simple numeric overestimate
 *     to determine k = floor(log10(d)).  We scale relevant
 *     quantities using O(log2(k)) rather than O(k) multiplications.
 *  2. For some modes > 2 (corresponding to ecvt and fcvt), we don't
 *     try to generate digits strictly left to right.  Instead, we
 *     compute with fewer bits and propagate the carry if necessary
 *     when rounding the final digit up.  This is often faster.
 *  3. Under the assumption that input will be rounded nearest,
 *     mode 0 renders 1e23 as 1e23 rather than 9.999999999999999e22.
 *     That is, we allow equality in stopping tests when the
 *     round-nearest rule will give the same floating-point value
 *     as would satisfaction of the stopping test with strict
 *     inequality.
 *  4. We remove common factors of powers of 2 from relevant
 *     quantities.
 *  5. When converting floating-point integers less than 1e16,
 *     we use floating-point arithmetic rather than resorting
 *     to multiple-precision integers.
 *  6. When asked to produce fewer than 15 digits, we first try
 *     to get by with floating-point arithmetic; we resort to
 *     multiple-precision integer arithmetic only if we cannot
 *     guarantee that the floating-point calculation has given
 *     the correctly rounded result.  For k requested digits and
 *     "uniformly" distributed input, the probability is
 *     something like 10^(k-15) that we must resort to the Long
 *     calculation.
 */


void
ruby_each_words(const char *str, void (*func)(const char*, int, void*), void *arg)
{
    const char *end;
    int len;

    if (!str) return;
    for (; *str; str = end) {
	while (ISSPACE(*str) || *str == ',') str++;
	if (!*str) break;
	end = str;
	while (*end && !ISSPACE(*end) && *end != ',') end++;
	len = (int)(end - str);	/* assume no string exceeds INT_MAX */
	(*func)(str, len, arg);
    }
}


VALUE
mrb_dbl_cmp(double a, double b)
{
    if (isnan(a) || isnan(b)) return MRB_TT_NIL;
    if (a == b) return INT2FIX(0);
    if (a > b) return INT2FIX(1);
    if (a < b) return INT2FIX(-1);
    return MRB_TT_NIL;
}

mrb_value
mrb_float_new(mrb_state *mrb, double d)
{
    mrb_value flt;
    struct RBasic *base = mrb_obj_alloc(mrb, MRB_TT_FLOAT, mrb->float_class);
    
    flt = mrb_obj_value(base);
    flt.value.f = d;
    return flt;
}
/*
static mrb_value
nurat_coerce(mrb_state *mrb, mrb_value self, mrb_value other)
{
    switch (mrb_type(other)) {
        case MRB_TT_FIXNUM:
        case MRB_TT_BIGNUM:
            return mrb_assoc_new(mrb,f_rational_new_bang1(mrb_obj_class(mrb,self), other), self);
        case MRB_TT_FLOAT:
            return rb_assoc_new(other, f_to_f(self));
        case MRB_TT_RATIONAL:
            return rb_assoc_new(other, self);
        case MRB_TT_COMPLEX:
            if (k_exact_zero_p(RCOMPLEX(other)->imag))
                return rb_assoc_new(f_rational_new_bang1
                                    (CLASS_OF(self), RCOMPLEX(other)->real), self);
            else
                return rb_assoc_new(other, rb_Complex(self, INT2FIX(0)));
    }
    
    rb_raise(rb_eTypeError, "%s can't be coerced into %s",
             rb_obj_classname(other), rb_obj_classname(self));
    return Qnil;
}


static mrb_value
coerce_body(mrb_state *mrb,mrb_value *x)
{
    return mrb_funcall(mrb, x[1], "coerce", 1, x[0]);
}

static VALUE
coerce_rescue(VALUE *x)
{
    volatile VALUE v = rb_inspect(x[1]);
    
    rb_raise(rb_eTypeError, "%s can't be coerced into %s",
             rb_special_const_p(x[1])?
             RSTRING_PTR(v):
             rb_obj_classname(x[1]),
             rb_obj_classname(x[0]));
    return Qnil;		
}

static int
do_coerce(VALUE *x, VALUE *y, int err)
{
    VALUE ary;
    VALUE a[2];
    
    a[0] = *x; a[1] = *y;
    
    ary = rb_rescue(coerce_body, (VALUE)a, err?coerce_rescue:0, (VALUE)a);
    if (TYPE(ary) != T_ARRAY || RARRAY_LEN(ary) != 2) {
        if (err) {
            rb_raise(rb_eTypeError, "coerce must return [x, y]");
        }
        return FALSE;
    }
    
    *x = RARRAY_PTR(ary)[0];
    *y = RARRAY_PTR(ary)[1];
    return TRUE;
}

VALUE
rb_num_coerce_bin(VALUE x, VALUE y, ID func)
{
    do_coerce(&x, &y, TRUE);
    return rb_funcall(x, func, 1, y);
}

VALUE
rb_num_coerce_cmp(VALUE x, VALUE y, ID func)
{
    if (do_coerce(&x, &y, FALSE))
        return rb_funcall(x, func, 1, y);
    return Qnil;
}

VALUE
rb_num_coerce_relop(VALUE x, VALUE y, ID func)
{
    VALUE c, x0 = x, y0 = y;
    
    if (!do_coerce(&x, &y, FALSE) ||
        NIL_P(c = rb_funcall(x, func, 1, y))) {
        rb_cmperr(x0, y0);
        return Qnil;		
    }
    return c;
}

*/
