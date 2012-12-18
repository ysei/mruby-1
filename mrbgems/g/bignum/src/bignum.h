//
//  bignum.h
//  mruby
//
//  Created by Christian Mauceri on 12/12/12.
//
//
#ifndef mruby_bignum_h
#define mruby_bignum_h

#include "mruby.h"
#include "util.h"
//#include "encoding.h"
#include "mruby/string.h"
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <math.h>
#include <float.h>
#include <ctype.h>
#include <stdio.h>
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif
#include <assert.h>

//mrb_malloc(mrb_state *mrb, size_t len)
//a = (struct RArray*)mrb_obj_alloc(mrb, MRB_TT_ARRAY, mrb->array_class);
//a->ptr = (mrb_value *)mrb_malloc(mrb, blen);

#define FL_USHIFT    12

#define FL_USER0     (((int)1)<<(FL_USHIFT+0))
#define FL_USER1     (((int)1)<<(FL_USHIFT+1))
#define FL_USER2     (((int)1)<<(FL_USHIFT+2))
#define FL_USER3     (((int)1)<<(FL_USHIFT+3))
#define FL_USER4     (((int)1)<<(FL_USHIFT+4))
#define FL_USER5     (((int)1)<<(FL_USHIFT+5))
#define FL_USER6     (((int)1)<<(FL_USHIFT+6))
#define FL_USER7     (((int)1)<<(FL_USHIFT+7))
#define FL_USER8     (((int)1)<<(FL_USHIFT+8))
#define FL_USER9     (((int)1)<<(FL_USHIFT+9))


static mrb_value bigsqr(mrb_state *mrb, mrb_value x);
static void bigdivmod(mrb_state *mrb, mrb_value x, mrb_value y, volatile mrb_value *divp, volatile mrb_value *modp);
mrb_value mrb_fix2str(mrb_state *mrb, mrb_value x, int base);
static mrb_value mrb_big_pow(mrb_state *mrb, mrb_value x, mrb_value y);

//#define CHAR_BIT SIZEOF_LONG
#define BDIGIT unsigned int
#define BDIGIT_DBL_SIGNED long

#define RBIGNUM_EMBED_LEN_MAX (int)((sizeof(VALUE)*3)/sizeof(BDIGIT))
#define RBIGNUM_EMBED_LEN_SHIFT (FL_USHIFT+3)
struct RBignum {
    MRB_OBJECT_HEADER;
    long len;
    BDIGIT *digits;
};

#define mrb_bignum_obj_alloc(mrb) ((struct RBignum*)mrb_obj_alloc((mrb), MRB_TT_BIGNUM, (mrb)->bignum_class))
#define mrb_bignum(b) ((struct RBignum *)((b).value.p))
static mrb_value mrb_bignum_alloc(mrb_state *mrb);




#define mrb_bignum_digits(b) mrb_bignum(b)->digits

#define RBIGNUM_SIGN_BIT FL_USER1
#define mrb_bignum_sign(b) ((mrb_basic(b)->flags & RBIGNUM_SIGN_BIT) != 0)

/* sign: positive:1, negative:0 */
#define mrb_bignum_set_sign(b,sign) ((sign) ? \
        (mrb_basic(b)->flags |= RBIGNUM_SIGN_BIT) : (mrb_basic(b)->flags &= ~RBIGNUM_SIGN_BIT))

#define mrb_bignum_positive(b) mrb_bignum_sign(b)
#define mrb_bignum_negative(b) (!mrb_bignum_sign(b))

#define RBIGNUM_EMBED_FLAG FL_USER2
#define RBIGNUM_EMBED_LEN_MASK (FL_USER5|FL_USER4|FL_USER3)
#define RBIGNUM_EMBED_LEN_SHIFT (FL_USHIFT+3)


#define mrb_bignum_len(b) mrb_bignum(b)->len

#define mrb_bignum_set_len(b,l) ((void)(mrb_bignum(b)->len = (l)))
#define mrb_bignum_set_digits(b,d) (mrb_bignum(b)->digits = (d))

#define mrb_bignum_movein(b,v,n) memcpy(mrb_bignum(b)->digits, v, sizeof(unsigned int)*(n))
#define mrb_bignum_moveout(v,b,n) memcpy(v,mrb_bignum(b)->digits, sizeof(unsigned int)*(n))


#define BITSPERDIG (SIZEOF_BDIGITS*CHAR_BIT)
#define BIGRAD ((BDIGIT_DBL)1 << BITSPERDIG)
#define BIGRAD_HALF ((BDIGIT)(BIGRAD >> 1))
#define DIGSPERLONG (SIZEOF_LONG/SIZEOF_BDIGITS)
#if HAVE_LONG_LONG
#define DIGSPERLL (SIZEOF_LONG_LONG/SIZEOF_BDIGITS)
#endif
#define BIGUP(x) ((BDIGIT_DBL)(x) << BITSPERDIG)
#define BIGDN(x) ((x) >> BITSPERDIG)
#define BIGLO(x) ((BDIGIT)((x) & (BIGRAD-1)))
#define BDIGMAX ((BDIGIT)-1)

#define BIGZEROP(x) (mrb_bignum_len(x) == 0 ||(mrb_bignum_digits(x)[0] == 0&&(mrb_bignum_len(x) == 1 || bigzero_p(x))))


#define RBIGNUM_EMBED_FLAG FL_USER2







#endif
