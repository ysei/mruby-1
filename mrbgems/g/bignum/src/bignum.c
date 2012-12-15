/**********************************************************************
 
 bignum.c -
 
 $Author: usa $
 created at: Fri Jun 10 00:48:55 JST 1994
 
 Copyright (C) 1993-2007 Yukihiro Matsumoto
 
 **********************************************************************/

#include "bignum.h"
#include "mruby/array.h"
#include "mruby/numeric.h"
#include "mruby/class.h"
#include "st.h"

#include <time.h>
#include <libc.h>
mrb_value
mrb_cstr_to_inum(mrb_state *mrb, const char *str, int base, int badcheck);
static mrb_value big_three ;

#define BDIGIT_DBL unsigned long

#if defined __MINGW32__
#define USHORT _USHORT
#endif

static mrb_value
bit_coerce(mrb_state *mrb, mrb_value x)
{
    while (!mrb_fixnum_p(x)) {
        if (mrb_float_p(x)) {
            mrb_raise(mrb, E_TYPE_ERROR, "can't convert Float into Integer");
        }
        x = mrb_to_int(mrb, x);
    }
    return x;
}

static mrb_value
mrb_bignum_alloc(mrb_state *mrb)
{
    struct RBignum* b;
    mrb_value res;
    
    b = mrb_bignum_obj_alloc(mrb);
    
    res.tt = MRB_TT_BIGNUM;
    res.value.p = b;
    return res;
}
static mrb_value
mrb_bignum_new(mrb_state *mrb, long len, int sign)
{
    mrb_value big = mrb_bignum_alloc(mrb);
    
    mrb_bignum_set_sign(big, sign?1:0);
    if (len <= RBIGNUM_EMBED_LEN_MAX) {
        mrb_basic(big)->flags |= RBIGNUM_EMBED_FLAG;
        mrb_bignum_set_len(big, len);
    }
    else {
        mrb_bignum(big)->as.heap.digits = mrb_alloca(mrb, len);
        mrb_bignum(big)->as.heap.len = len;
    }
    return big;
}

static void
dump_bignum(mrb_state *mrb, mrb_value x)
{
    long i;
    printf("%c0x0", mrb_bignum_len(x) ? '+' : '-');
    for (i = mrb_bignum_len(x); i--; ) {
        printf("_%08""x", mrb_bignum_digits(x)[i]);
    }
    printf(", len=%lu", mrb_bignum_len(x));
    puts("");
}

static mrb_value
mrb_big_dump(mrb_state *mrb, mrb_value x)
{
    dump_bignum(mrb, x);
    return x;
}

static int
bigzero_p(mrb_value x)
{
    long i;

    unsigned int *ds = mrb_bignum_digits(x);
    
    for (i = mrb_bignum_len(x) - 1; 0 <= i; i--) {
        if (ds[i]) return 0;
    }
    return 1;
}

int
mrb_bigzero_p(mrb_state *mrb, mrb_value x)
{
    return bigzero_p(x);
}


static void
mrb_big_realloc(mrb_state *mrb, mrb_value big, long len)
{
    unsigned int *ds;
    if (mrb_basic(big)->flags & RBIGNUM_EMBED_FLAG) {
        if (RBIGNUM_EMBED_LEN_MAX < len) {
            ds = mrb_alloca(mrb, len);
            mrb_bignum_moveout(ds, big, RBIGNUM_EMBED_LEN_MAX);
            mrb_bignum_set_len(big, mrb_bignum_len(big));
            mrb_bignum_set_digits(big,ds);
            mrb_basic(big)->flags &= ~RBIGNUM_EMBED_FLAG;
        }
    }
    else {
        if (len <= RBIGNUM_EMBED_LEN_MAX) {
            ds = mrb_bignum(big)->as.heap.digits;
            mrb_basic(big)->flags |= RBIGNUM_EMBED_FLAG;
            mrb_bignum_set_len(big, len);
            if (ds) {
                mrb_bignum_movein(big, ds, len);
                free(ds);
            }
        }
        else {
            if (mrb_bignum_len(big) == 0) {
                mrb_bignum(big)->as.heap.digits = mrb_alloca(mrb, len);
            }
            else {
                mrb_realloc(mrb,mrb_bignum(big)->as.heap.digits, len);
            }
        }
    }
}

void
mrb_big_resize(mrb_state *mrb, mrb_value big, long len)
{
    mrb_big_realloc(mrb,big, len);
    mrb_bignum_set_len(big, len);
}



mrb_value
mrb_big_clone(mrb_state *mrb, mrb_value x)
{
    long len = mrb_bignum_len(x);
    mrb_value z = mrb_bignum_new(mrb,len, mrb_bignum_sign(x));
    
    mrb_bignum_movein(z, mrb_bignum_digits(x), len);
    return z;
}

/* modify a bignum by 2's complement */
static void
get2comp(mrb_state *mrb, mrb_value x)
{
    long i = mrb_bignum_len(x);
    BDIGIT *ds = mrb_bignum_digits(x);
    BDIGIT_DBL num;
    
    if (!i) return;
    while (i--) ds[i] = ~ds[i];
    i = 0; num = 1;
    do {
        num += ds[i];
        ds[i++] = BIGLO(num);
        num = BIGDN(num);
    } while (i < mrb_bignum_len(x));
    if (num != 0) {
        mrb_big_resize(mrb,x, mrb_bignum_len(x)+1);
        ds = mrb_bignum_digits(x);
        ds[mrb_bignum_len(x)-1] = 1;
    }
}

void
mrb_big_2comp(mrb_state *mrb, mrb_value x)			/* get 2's complement */
{
    get2comp(mrb,x);
}

static inline mrb_value
bigtrunc(mrb_state *mrb, mrb_value x)
{
    long len = mrb_bignum_len(x);
    BDIGIT *ds = mrb_bignum_digits(x);
    
    if (len == 0) return x;
    while (--len && !ds[len]);
    if (mrb_bignum_len(x) > len+1) {
        mrb_big_resize(mrb,x, len+1);
    }
    return x;
}

static inline VALUE
bigfixize(mrb_state *mrb, mrb_value x)
{
    long len = mrb_bignum_len(x);
    BDIGIT *ds = mrb_bignum_digits(x);
    
    if (len == 0) return INT2FIX(0);
    if ((size_t)(len*SIZEOF_BDIGITS) <= sizeof(long)) {
        long num = 0;
#if 2*SIZEOF_mrb_bignum_digits > SIZEOF_LONG
        num = (long)ds[0];
#else
        while (len--) {
            num = (long)(BIGUP(num) + ds[len]);
        }
#endif
        if (num >= 0) {
            if (mrb_bignum_sign(x)) {
                if (POSFIXABLE(num)) return LONG2FIX(num);
            }
            else {
                if (NEGFIXABLE(-num)) return LONG2FIX(-num);
            }
        }
    }
    return x.value.i;
}

static mrb_value
bignorm(mrb_state *mrb, mrb_value x)
{
    if (!mrb_fixnum_p(x) && mrb_type(x) == MRB_TT_BIGNUM) {
        x.value.i = bigfixize(mrb,x);
    }
    return x;
}

mrb_value
rb_big_norm(mrb_state *mrb, mrb_value x)
{
    return bignorm(mrb,x);
}

mrb_value
mrb_uint2big(mrb_state *mrb, mrb_value n)
{
    BDIGIT_DBL num = n.value.i;
    long i = 0;
    BDIGIT *digits;
    mrb_value big;
    
    big = mrb_bignum_new(mrb,DIGSPERLONG, 1);
    digits = mrb_bignum_digits(big);
    while (i < DIGSPERLONG) {
        digits[i++] = BIGLO(num);
        num = BIGDN(num);
    }
    
    i = DIGSPERLONG;
    while (--i && !digits[i]) ;
    mrb_bignum_set_len(big, i+1);
    return big;
}

mrb_value
mrb_int2big(mrb_state *mrb, mrb_value n)
{
    mrb_value num = n;
    long neg = 0;
    mrb_value big;
    
    if (num.value.i < 0) {
        num.value.i = -num.value.i;
        neg = 1;
    }
    big = mrb_uint2big(mrb,num);
    if (neg) {
        mrb_bignum_set_sign(big, 0);
    }
    return big;
}

mrb_value
mrb_uint2inum(mrb_state *mrb, mrb_value n)
{
    if (POSFIXABLE(n.value.i)) {
        mrb_value res;
        res.value.i = LONG2FIX(n.value.i);
        return res;
    }
    return mrb_uint2big(mrb,n);
}

mrb_value
mrb_int2inum(mrb_state *mrb, mrb_value n)
{
    if (FIXABLE(n.value.i)) {
        mrb_value res;
        res.value.i = LONG2FIX(n.value.i);
        return res;
    }
    return mrb_int2big(mrb,n);
}


#if SIZEOF_LONG % SIZEOF_BDIGITS != 0
# error unexpected SIZEOF_LONG : SIZEOF_BDIGITS ratio
#endif

/*
 * buf is an array of long integers.
 * buf is ordered from least significant word to most significant word.
 * buf[0] is the least significant word and
 * buf[num_longs-1] is the most significant word.
 * This means words in buf is little endian.
 * However each word in buf is native endian.
 * (buf[i]&1) is the least significant bit and
 * (buf[i]&(1<<(SIZEOF_LONG*CHAR_BIT-1))) is the most significant bit
 * for each 0 <= i < num_longs.
 * So buf is little endian at whole on a little endian machine.
 * But buf is mixed endian on a big endian machine.
 */
void
mrb_big_pack(mrb_state *mrb, mrb_value val, unsigned long *buf, long num_longs)
{
    val = mrb_to_int(mrb,val);
    if (num_longs == 0)
        return;
    if (mrb_fixnum_p(val)) {
        long i;
        long tmp = FIX2LONG(val.value.i);
        buf[0] = (unsigned long)tmp;
        tmp = tmp < 0 ? ~0L : 0;
        for (i = 1; i < num_longs; i++)
            buf[i] = (unsigned long)tmp;
        return;
    }
    else {
        long len = mrb_bignum_len(val);
        BDIGIT *ds = mrb_bignum_digits(val), *dend = ds + len;
        long i, j;
        for (i = 0; i < num_longs && ds < dend; i++) {
            unsigned long l = 0;
            for (j = 0; j < DIGSPERLONG && ds < dend; j++, ds++) {
                l |= ((unsigned long)*ds << (j * BITSPERDIG));
            }
            buf[i] = l;
        }
        for (; i < num_longs; i++)
            buf[i] = 0;
        if (mrb_bignum_positive(val)) {
            for (i = 0; i < num_longs; i++) {
                buf[i] = ~buf[i];
            }
            for (i = 0; i < num_longs; i++) {
                buf[i]++;
                if (buf[i] != 0)
                    return;
            }
        }
    }
}

static inline mrb_value
LONG2NUM(mrb_state *mrb,long v)
{
    mrb_value mv ;
    mv.value.i = v;
    if(FIXABLE(v)) {
        mrb_value res;
        res.value.i = LONG2FIX(v);
        return res;
    }
    return mrb_int2big(mrb,mv);
}

/* See rb_big_pack comment for endianness of buf. */
mrb_value
mrb_big_unpack(mrb_state *mrb, unsigned long *buf, long num_longs)
{
    while (2 <= num_longs) {
        if (buf[num_longs-1] == 0 && (long)buf[num_longs-2] >= 0)
            num_longs--;
        else if (buf[num_longs-1] == ~0UL && (long)buf[num_longs-2] < 0)
            num_longs--;
        else
            break;
    }
    if (num_longs == 0) {
        mrb_value res;
        res.value.i = INT2FIX(0);
        return res ;
    }
    else if (num_longs == 1) {
        return LONG2NUM(mrb,(long)buf[0]);
    }
    else {
        mrb_value big;
        BDIGIT *ds;
        long len = num_longs * DIGSPERLONG;
        long i;
        big = mrb_bignum_new(mrb,len, 1);
        ds = mrb_bignum_digits(big);
        for (i = 0; i < num_longs; i++) {
            unsigned long d = buf[i];
#if SIZEOF_LONG == SIZEOF_mrb_bignum_digits
            *ds++ = d;
#else
            int j;
            for (j = 0; j < DIGSPERLONG; j++) {
                *ds++ = BIGLO(d);
                d = BIGDN(d);
            }
#endif
        }
        if ((long)buf[num_longs-1] < 0) {
            get2comp(mrb,big);
            mrb_bignum_set_sign(big, 0);
        }
        return bignorm(mrb,big);
    }
}

#define QUAD_SIZE 8

#if SIZEOF_LONG_LONG == QUAD_SIZE && SIZEOF_mrb_bignum_digits*2 == SIZEOF_LONG_LONG

void
mrb_quad_pack(mrb_state *mrb, char *buf, mrb_value val)
{
    LONG_LONG q;
    
    val = rb_to_int(val);
    if (mrb_fixnum_p(val)) {
        q = FIX2LONG(val);
    }
    else {
        long len = mrb_bignum_len(val);
        BDIGIT *ds;
        
        if (len > SIZEOF_LONG_LONG/SIZEOF_mrb_bignum_digits) {
            len = SIZEOF_LONG_LONG/SIZEOF_mrb_bignum_digits;
        }
        ds = mrb_bignum_digits(val);
        q = 0;
        while (len--) {
            q = BIGUP(q);
            q += ds[len];
        }
        if (!mrb_bignum_sign(val)) q = -q;
    }
    memcpy(buf, (char*)&q, SIZEOF_LONG_LONG);
}

mrb_value
mrb_quad_unpack(mrb_state *mrb, const char *buf, int sign)
{
    unsigned LONG_LONG q;
    long neg = 0;
    long i;
    BDIGIT *digits;
    mrb_value big;
    
    memcpy(&q, buf, SIZEOF_LONG_LONG);
    if (sign) {
        if (FIXABLE((LONG_LONG)q)) return LONG2FIX((LONG_LONG)q);
        if ((LONG_LONG)q < 0) {
            q = -(LONG_LONG)q;
            neg = 1;
        }
    }
    else {
        if (POSFIXABLE(q)) return LONG2FIX(q);
    }
    
    i = 0;
    big = mrb_bignum_new(mrb,DIGSPERLL, 1);
    digits = mrb_bignum_digits(big);
    while (i < DIGSPERLL) {
        digits[i++] = BIGLO(q);
        q = BIGDN(q);
    }
    
    i = DIGSPERLL;
    while (i-- && !digits[i]) ;
    mrb_bignum_set_len(big, i+1);
    
    if (neg) {
        mrb_bignum_set_sign(big, 0);
    }
    return bignorm(mrb,big);
}

#else

static int
quad_buf_complement(mrb_state *mrb, char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        buf[i] = ~buf[i];
    for (i = 0; i < len; i++) {
        buf[i]++;
        if (buf[i] != 0)
            return 0;
    }
    return 1;
}

void
mrb_quad_pack(mrb_state *mrb, char *buf, mrb_value val)
{
    long len;
    
    memset(buf, 0, QUAD_SIZE);
    val = mrb_to_int(mrb,val);
    if (mrb_fixnum_p(val)) {
        val.value.i = FIX2LONG(val.value.i);
        val = mrb_int2big(mrb,val);
    }
    len = mrb_bignum_len(val) * SIZEOF_BDIGITS;
    if (len > QUAD_SIZE) {
        len = QUAD_SIZE;
    }
    memcpy(buf, (char*)mrb_bignum_digits(val), len);
    if (mrb_bignum_negative(val)) {
        quad_buf_complement(mrb,buf, QUAD_SIZE);
    }
}

#define BNEG(b) ((((BDIGIT*)(b))[QUAD_SIZE/SIZEOF_BDIGITS-1]>>BITSPERDIG-1) != 0)

mrb_value
mrb_quad_unpack(mrb_state *mrb, const char *buf, int sign)
{
    mrb_value big = mrb_bignum_new(mrb,QUAD_SIZE/SIZEOF_BDIGITS,sign);
    mrb_bignum_movein(big, buf, QUAD_SIZE);
    if (sign && BNEG(buf)) {
        char *tmp = (char*)mrb_bignum_digits(big);
        
        mrb_bignum_set_sign(big, 0);
        quad_buf_complement(mrb,tmp, QUAD_SIZE);
    }
    
    return bignorm(mrb,big);
}

#endif

#if HAVE_LONG_LONG

static mrb_value
mrb_ull2big(mrb_state *mrb, unsigned LONG_LONG n)
{
    BDIGIT_DBL num = n;
    long i = 0;
    BDIGIT *digits;
    mrb_value big;
    
    big = mrb_bignum_new(mrb,DIGSPERLL, 1);
    digits = mrb_bignum_digits(big);
    while (i < DIGSPERLL) {
        digits[i++] = BIGLO(num);
        num = BIGDN(num);
    }
    
    i = DIGSPERLL;
    while (i-- && !digits[i]) ;
    mrb_bignum_set_len(big, i+1);
    return big;
}

static mrb_value
mrb_ll2big(mrb_state *mrb, LONG_LONG n)
{
    long neg = 0;
    mrb_value big;
    
    if (n < 0) {
        n = -n;
        neg = 1;
    }
    big = rb_ull2big(n);
    if (neg) {
        mrb_bignum_set_sign(big, 0);
    }
    return big;
}

mrb_value
mrb_ull2inum(mrb_state *mrb, unsigned LONG_LONG n)
{
    if (POSFIXABLE(n)) return LONG2FIX(n);
    return rb_ull2big(n);
}

mrb_value
mrb_ll2inum(mrb_state *mrb, LONG_LONG n)
{
    if (FIXABLE(n)) return LONG2FIX(n);
    return rb_ll2big(n);
}

#endif  /* HAVE_LONG_LONG */

mrb_value
mrb_cstr2inum(mrb_state *mrb, const char *str, int base)
{
    return mrb_cstr_to_inum(mrb,str, base, base==0);
}

mrb_value
mrb_str2inum(mrb_state *mrb, mrb_value str, int base)
{
    return mrb_str_to_inum(mrb,str, base, base==0);
}

const char ruby_digitmap[] = "0123456789abcdefghijklmnopqrstuvwxyz";


#define POW2_P(x) (((x)&((x)-1))==0)

static inline int
ones(mrb_state *mrb, register unsigned long x)
{
#if SIZEOF_LONG == 8
# define MASK_55 0x5555555555555555UL
# define MASK_33 0x3333333333333333UL
# define MASK_0f 0x0f0f0f0f0f0f0f0fUL
#else
# define MASK_55 0x55555555UL
# define MASK_33 0x33333333UL
# define MASK_0f 0x0f0f0f0fUL
#endif
    x -= (x >> 1) & MASK_55;
    x = ((x >> 2) & MASK_33) + (x & MASK_33);
    x = ((x >> 4) + x) & MASK_0f;
    x += (x >> 8);
    x += (x >> 16);
#if SIZEOF_LONG == 8
    x += (x >> 32);
#endif
    return (int)(x & 0x7f);
#undef MASK_0f
#undef MASK_33
#undef MASK_55
}

static inline unsigned long
next_pow2(mrb_state *mrb, register unsigned long x)
{
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if SIZEOF_LONG == 8
    x |= x >> 32;
#endif
    return x + 1;
}

static inline int
floor_log2(mrb_state *mrb, register unsigned long x)
{
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if SIZEOF_LONG == 8
    x |= x >> 32;
#endif
    return (int)ones(mrb,x) - 1;
}

static inline int
ceil_log2(mrb_state *mrb, register unsigned long x)
{
    return floor_log2(mrb,x) + !POW2_P(x);
}

#define LOG2_KARATSUBA_DIGITS 7
#define KARATSUBA_DIGITS (1L<<LOG2_KARATSUBA_DIGITS)
#define MAX_BIG2STR_TABLE_ENTRIES 64
static mrb_value big2str_power_cache[35][MAX_BIG2STR_TABLE_ENTRIES];

static void
power_cache_init(mrb_state *mrb )
{
    int i, j;
    for (i = 0; i < 35; ++i) {
        for (j = 0; j < MAX_BIG2STR_TABLE_ENTRIES; ++j) {
            big2str_power_cache[i][j].value.i = MRB_TT_NIL;
        }
    }
}

static inline mrb_value
power_cache_get_power0(mrb_state *mrb, int base, int i)
{
    mrb_value mrbase;
    mrb_value mrp;
    mrb_value mz;
    mrb_value mv;
    if (mrb_nil_p(big2str_power_cache[base - 2][i])) {
        
        mrbase.value.i = base;
        mrp.value.i = i-1;
        mz.value.i = INT2FIX(KARATSUBA_DIGITS);
        mv =
	    i == 0 ? mrb_big_pow(mrb,mrb_int2big(mrb,mrbase), mz) : bigsqr(mrb,power_cache_get_power0(mrb,base, i-1));
        big2str_power_cache[base - 2][i] = mv;
        mrb_gc_mark(mrb, mrb_basic(big2str_power_cache[base - 2][i]));
       
    }
    return big2str_power_cache[base - 2][i];
}

static mrb_value
power_cache_get_power(mrb_state *mrb, int base, long n1, long* m1)
{
    int i, m;
    long j;
    mrb_value t;
    
    if (n1 <= KARATSUBA_DIGITS)
        mrb_raise(mrb, mrb->bignum_class,"n1 > KARATSUBA_DIGITS");
    
    m = ceil_log2(mrb,n1);
    if (m1) *m1 = 1 << m;
    i = m - LOG2_KARATSUBA_DIGITS;
    if (i >= MAX_BIG2STR_TABLE_ENTRIES)
        i = MAX_BIG2STR_TABLE_ENTRIES - 1;
    t = power_cache_get_power0(mrb,base, i);
    
    j = KARATSUBA_DIGITS*(1 << i);
    while (n1 > j) {
        t = bigsqr(mrb,t);
        j *= 2;
    }
    return t;
}

/* big2str_muraken_find_n1
 *
 * Let a natural number x is given by:
 * x = 2^0 * x_0 + 2^1 * x_1 + ... + 2^(B*n_0 - 1) * x_{B*n_0 - 1},
 * where B is BITSPERDIG (i.e. mrb_bignum_digits*CHAR_BIT) and n_0 is
 * mrb_bignum_len(x).
 *
 * Now, we assume n_1 = min_n \{ n | 2^(B*n_0/2) <= b_1^(n_1) \}, so
 * it is realized that 2^(B*n_0) <= {b_1}^{2*n_1}, where b_1 is a
 * given radix number. And then, we have n_1 <= (B*n_0) /
 * (2*log_2(b_1)), therefore n_1 is given by ceil((B*n_0) /
 * (2*log_2(b_1))).
 */
static long
big2str_find_n1(mrb_state *mrb, mrb_value x, int base)
{
    static const double log_2[] = {
        1.0,              1.58496250072116, 2.0,
        2.32192809488736, 2.58496250072116, 2.8073549220576,
        3.0,              3.16992500144231, 3.32192809488736,
        3.4594316186373,  3.58496250072116, 3.70043971814109,
        3.8073549220576,  3.90689059560852, 4.0,
        4.08746284125034, 4.16992500144231, 4.24792751344359,
        4.32192809488736, 4.39231742277876, 4.4594316186373,
        4.52356195605701, 4.58496250072116, 4.64385618977472,
        4.70043971814109, 4.75488750216347, 4.8073549220576,
        4.85798099512757, 4.90689059560852, 4.95419631038688,
        5.0,              5.04439411935845, 5.08746284125034,
        5.12928301694497, 5.16992500144231
    };
    long bits;
    
    if (base < 2 || 36 < base)
        mrb_raisef(mrb, mrb->bignum_class, "invalid radix %d", base);
    
    if (mrb_fixnum_p(x)) {
        bits = (SIZEOF_LONG*CHAR_BIT - 1)/2 + 1;
    }
    else if (BIGZEROP(x)) {
        return 0;
    }
    else if (mrb_bignum_len(x) >= LONG_MAX/BITSPERDIG) {
        mrb_raise(mrb,mrb->bignum_class, "bignum too big to convert into `string'");
    }
    else {
        bits = BITSPERDIG*mrb_bignum_len(x);
    }
    
    return (long)ceil(bits/log_2[base - 2]);
}

static long
big2str_orig(mrb_state *mrb, mrb_value x, int base, char* ptr, long len, long hbase, int trim)
{
    long i = mrb_bignum_len(x), j = len;
    BDIGIT* ds = mrb_bignum_digits(x);
    
    while (i && j > 0) {
        long k = i;
        BDIGIT_DBL num = 0;
        
        while (k--) {               /* x / hbase */
            num = BIGUP(num) + ds[k];
            ds[k] = (BDIGIT)(num / hbase);
            num %= hbase;
        }
        if (trim && ds[i-1] == 0) i--;
        k = SIZEOF_BDIGITS;
        while (k--) {
            ptr[--j] = ruby_digitmap[num % base];
            num /= base;
            if (j <= 0) break;
            if (trim && i == 0 && num == 0) break;
        }
    }
    if (trim) {
        while (j < len && ptr[j] == '0') j++;
        MEMMOVE(ptr, ptr + j, char, len - j);
        len -= j;
    }
    return len;
}

static long
big2str_karatsuba(mrb_state *mrb, mrb_value x, int base, char* ptr,
                  long n1, long len, long hbase, int trim)
{
    long lh, ll, m1;
    mrb_value b, q, r;
    
    if (BIGZEROP(x)) {
        if (trim) return 0;
        else {
            memset(ptr, '0', len);
            return len;
        }
    }
    
    if (n1 <= KARATSUBA_DIGITS) {
        return big2str_orig(mrb,x, base, ptr, len, hbase, trim);
    }
    
    b = power_cache_get_power(mrb,base, n1, &m1);
    bigdivmod(mrb,x, b, &q, &r);
    lh = big2str_karatsuba(mrb,q, base, ptr, (len - m1)/2,
                           len - m1, hbase, trim);
    mrb_big_resize(mrb,q, 0);
    ll = big2str_karatsuba(mrb,r, base, ptr + lh, m1/2,
                           m1, hbase, !lh && trim);
    mrb_big_resize(mrb,r, 0);
    
    return lh + ll;
}

mrb_value
mrb_big2str0(mrb_state *mrb, mrb_value x, int base, int trim)
{
    int off;
    mrb_value ss, xx;
    long n1, n2, len, hbase;
    
    if (mrb_fixnum_p(x)) {
        mrb_value res;
        res.value.i = base;
        res = mrb_fix2str(mrb,x, base);
        return res;
    }
    if (BIGZEROP(x)) {
        mrb_value res = mrb_usascii_str_new_cstr(mrb, "0");
        return res;
    }
    
    if (base < 2 || 36 < base)
        mrb_raisef(mrb,mrb->bignum_class, "invalid radix %d", base);
    
    n2 = big2str_find_n1(mrb,x, base);
    n1 = (n2 + 1) / 2;
    char *str = mrb_alloca(mrb, n2 + 1);
    ss = mrb_usascii_str_new_cstr(mrb, str);
    str[0] = mrb_bignum_sign(x) ? '+' : '-';
    
    hbase = base*base;
#if SIZEOF_mrb_bignum_digits > 2
    hbase *= hbase;
#endif
    off = !(trim && mrb_bignum_sign(x)); /* erase plus sign if trim */
    xx = mrb_big_clone(mrb,x);
    mrb_bignum_set_sign(xx, 1);
    if (n1 <= KARATSUBA_DIGITS) {
        len = off + big2str_orig(mrb,xx, base, str + off, n2, hbase, trim);
    }
    else {
        len = off + big2str_karatsuba(mrb,xx, base, str + off, n1, n2, hbase, trim);
    }
    mrb_big_resize(mrb,xx, 0);
    
    str[len] = '\0';
    mrb_str_resize(mrb,ss, len);
    
    return ss;
}

mrb_value
mrb_big2str(mrb_state *mrb, mrb_value x, int base)
{
    return mrb_big2str0(mrb,x, base, 1);
}

/*
 *  call-seq:
 *     big.to_s(base=10)   ->  string
 *
 *  Returns a string containing the representation of <i>big</i> radix
 *  <i>base</i> (2 through 36).
 *
 *     12345654321.to_s         #=> "12345654321"
 *     12345654321.to_s(2)      #=> "1011011111110110111011110000110001"
 *     12345654321.to_s(8)      #=> "133766736061"
 *     12345654321.to_s(16)     #=> "2dfdbbc31"
 *     78546939656932.to_s(36)  #=> "rubyrules"
 */
//
#define NUM2INT(x) (mrb_fixnum_p(x) ? FIX2LONG(x) : (int)rb_num2int(x))


static mrb_value
mrb_big_to_s(mrb_state *mrb, int argc, mrb_value *argv, mrb_value x)
{
    int base;
    
    if (argc == 0) base = 10;
    else {
        mrb_int i;
        mrb_get_args(mrb, "i", &i);
        base = mrb_fixnum_value(i).value.i;
    }
    return mrb_big2str(mrb, x, base);
}

mrb_value
n_mrb_big_to_s (mrb_state *mrb,mrb_value self)
{
    return mrb_big_to_s(mrb, 0, NULL, self);
}

static mrb_value
big2ulong(mrb_state *mrb, mrb_value x, const char *type, int check)
{
    long len = mrb_bignum_len(x);
    BDIGIT_DBL num;
    BDIGIT *ds;
    mrb_value res;
    
    if (len > DIGSPERLONG) {
        if (check)
            mrb_raisef(mrb,mrb->bignum_class, "bignum too big to convert into `%s'", type);
        len = DIGSPERLONG;
    }
    ds = mrb_bignum_digits(x);
    num = 0;
    while (len--) {
        num = BIGUP(num);
        num += ds[len];
    }
    res.value.i = num;
    return res;
}

mrb_value
mrb_big2ulong_pack(mrb_state *mrb, mrb_value x)
{
    mrb_value num = big2ulong(mrb,x, "unsigned long", FALSE);
    if (!mrb_bignum_sign(x)) {
        num.value.i = (-(SIGNED_VALUE)num.value.i);
        return num;
    }
    return num;
}

mrb_value
mrb_big2ulong(mrb_state *mrb, mrb_value x)
{
    mrb_value num = big2ulong(mrb,x, "unsigned long", TRUE);
    
    if (!mrb_bignum_sign(x)) {
        if ((long)num.value.i < 0) {
            mrb_raise(mrb,mrb->bignum_class, "bignum out of range of unsigned long");
        }
        num.value.i = (-(SIGNED_VALUE)num.value.i);
        return num;
    }
    return num;
}

mrb_value
mrb_big2long(mrb_state *mrb, mrb_value x)
{
    mrb_value num = big2ulong(mrb,x, "long", TRUE);
    
    if ((long)num.value.i < 0 &&
        (mrb_bignum_sign(x) || (long)num.value.i != LONG_MIN)) {
        mrb_raise(mrb,mrb->bignum_class, "bignum too big to convert into `long'");
    }
    if (!mrb_bignum_sign(x)) {
        num.value.i = -(SIGNED_VALUE)num.value.i;
        return num;
    }
    return num;
}

#if HAVE_LONG_LONG

static unsigned LONG_LONG
big2ull(mrb_state *mrb, mrb_value x, const char *type)
{
    long len = mrb_bignum_len(x);
    BDIGIT_DBL num;
    BDIGIT *ds;
    
    if (len > SIZEOF_LONG_LONG/SIZEOF_mrb_bignum_digits)
        rb_raise(rb_eRangeError, "bignum too big to convert into `%s'", type);
    ds = mrb_bignum_digits(x);
    num = 0;
    while (len--) {
        num = BIGUP(num);
        num += ds[len];
    }
    return num;
}

unsigned LONG_LONG
mrb_big2ull(mrb_state *mrb, mrb_value x)
{
    unsigned LONG_LONG num = big2ull(x, "unsigned long long");
    
    if (!mrb_bignum_sign(x))
        return (mrb_value)(-(SIGNED_mrb_value)num);
    return num;
}

LONG_LONG
mrb_big2ll(mrb_state *mrb, mrb_value x)
{
    unsigned LONG_LONG num = big2ull(x, "long long");
    
    if ((LONG_LONG)num < 0 && (mrb_bignum_sign(x)
                               || (LONG_LONG)num != LLONG_MIN)) {
        rb_raise(rb_eRangeError, "bignum too big to convert into `long long'");
    }
    if (!mrb_bignum_sign(x)) return -(LONG_LONG)num;
    return num;
}

#endif  /* HAVE_LONG_LONG */

static mrb_value
dbl2big(mrb_state *mrb, double d)
{
    long i = 0;
    BDIGIT c;
    BDIGIT *digits;
    mrb_value z;
    double u = (d < 0)?-d:d;
    
    if (isinf(d)) {
        mrb_raise(mrb,mrb->bignum_class, d < 0 ? "-Infinity" : "Infinity");
    }
    if (isnan(d)) {
        mrb_raise(mrb,mrb->bignum_class, "NaN");
    }
    
    while (!POSFIXABLE(u) || 0 != (long)u) {
        u /= (double)(BIGRAD);
        i++;
    }
    z = mrb_bignum_new(mrb,i, d>=0);
    digits = mrb_bignum_digits(z);
    while (i--) {
        u *= BIGRAD;
        c = (BDIGIT)u;
        u -= c;
        digits[i] = c;
    }
    
    return z;
}

mrb_value
mrb_dbl2big(mrb_state *mrb, double d)
{
    return bignorm(mrb,dbl2big(mrb,d));
}

static int
nlz(BDIGIT x)
{
    BDIGIT y;
    int n = BITSPERDIG;
#if BITSPERDIG > 64
    y = x >> 64; if (y) {n -= 64; x = y;}
#endif
#if BITSPERDIG > 32
    y = x >> 32; if (y) {n -= 32; x = y;}
#endif
#if BITSPERDIG > 16
    y = x >> 16; if (y) {n -= 16; x = y;}
#endif
    y = x >>  8; if (y) {n -=  8; x = y;}
    y = x >>  4; if (y) {n -=  4; x = y;}
    y = x >>  2; if (y) {n -=  2; x = y;}
    y = x >>  1; if (y) {return n - 2;}
    return n - x;
}

static double
big2dbl(mrb_state *mrb, mrb_value x)
{
    double d = 0.0;
    long i = (bigtrunc(mrb,x), mrb_bignum_len(x)), lo = 0, bits;
    BDIGIT *ds = mrb_bignum_digits(x), dl;
    
    if (i) {
        bits = i * BITSPERDIG - nlz(ds[i-1]);
        if (bits > DBL_MANT_DIG+DBL_MAX_EXP) {
            d = HUGE_VAL;
        }
        else {
            if (bits > DBL_MANT_DIG+1)
                lo = (bits -= DBL_MANT_DIG+1) / BITSPERDIG;
            else
                bits = 0;
            while (--i > lo) {
                d = ds[i] + BIGRAD*d;
            }
            dl = ds[i];
            if (bits && (dl & (1UL << (bits %= BITSPERDIG)))) {
                int carry = dl & ~(~(BDIGIT)0 << bits);
                if (!carry) {
                    while (i-- > 0) {
                        if ((carry = ds[i]) != 0) break;
                    }
                }
                if (carry) {
                    dl &= (BDIGIT)~0 << bits;
                    dl += (BDIGIT)1 << bits;
                    if (!dl) d += 1;
                }
            }
            d = dl + BIGRAD*d;
            if (lo) {
                if (lo > INT_MAX / BITSPERDIG)
                    d = HUGE_VAL;
                else if (lo < INT_MIN / BITSPERDIG)
                    d = 0.0;
                else
                    d = ldexp(d, (int)(lo * BITSPERDIG));
            }
        }
    }
    if (!mrb_bignum_sign(x)) d = -d;
    return d;
}

double
mrb_big2dbl(mrb_state *mrb, mrb_value x)
{
    double d = big2dbl(mrb,x);
    
    if (isinf(d)) {
        printf("Bignum out of Float range\n");
        if (d < 0.0)
            d = -HUGE_VAL;
        else
            d = HUGE_VAL;
    }
    return d;
}

/*
 *  call-seq:
 *     big.to_f -> float
 *
 *  Converts <i>big</i> to a <code>Float</code>. If <i>big</i> doesn't
 *  fit in a <code>Float</code>, the result is infinity.
 *
 */

static mrb_value
mrb_big_to_f(mrb_state *mrb, mrb_value x)
{
    mrb_value res;
    res.value.f = mrb_big2dbl(mrb,x);
    return res;
}

/*
 *  call-seq:
 *     big <=> numeric   -> -1, 0, +1 or nil
 *
 *  Comparison---Returns -1, 0, or +1 depending on whether <i>big</i> is
 *  less than, equal to, or greater than <i>numeric</i>. This is the
 *  basis for the tests in <code>Comparable</code>.
 *
 */

mrb_value
mrb_big_cmp(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long xlen = mrb_bignum_len(x);
    BDIGIT *xds, *yds;
    mrb_value res;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y.value.i = FIX2LONG(y.value.i);
            y = mrb_int2big(mrb,y);
            break;
            
        case MRB_TT_BIGNUM:
            break;
            
        case MRB_TT_FLOAT:
        {
            double a = y.value.f;
            
            if (isinf(a)) {
                if (a > 0.0) {
                    res.value.f = INT2FIX(-1);
                    return res;
                }
                else  {
                    if (a > 0.0) {
                        res.value.f = INT2FIX(-1);
                        return res;
                    }
                }
            }
            res.value.f = mrb_dbl_cmp(mrb_big2dbl(mrb,x), a);
            return res;
        }
            
        default:
            return mrb_funcall(mrb, x, "<=>", 1, y);
    }
    
    if (mrb_bignum_sign(x) > mrb_bignum_sign(y)) return mrb_fixnum_value(1);
    if (mrb_bignum_sign(x) < mrb_bignum_sign(y)) return mrb_fixnum_value(-1);
    if (xlen < mrb_bignum_len(y))
        return (mrb_bignum_sign(x)) ? mrb_fixnum_value(INT2FIX(-1)) : mrb_fixnum_value(INT2FIX(1));
    if (xlen > mrb_bignum_len(y))
        return (mrb_bignum_sign(x)) ? mrb_fixnum_value(INT2FIX(1)) : mrb_fixnum_value(INT2FIX(-1));
    
    xds = mrb_bignum_digits(x);
    yds = mrb_bignum_digits(y);
    
    while(xlen-- && (xds[xlen]==yds[xlen]));
    if (-1 == xlen) return mrb_fixnum_value(INT2FIX(0));
    return (xds[xlen] > yds[xlen]) ?
	(mrb_bignum_sign(x) ? mrb_fixnum_value(INT2FIX(1)) : mrb_fixnum_value(INT2FIX(-1))) :
    (mrb_bignum_sign(x) ? mrb_fixnum_value(INT2FIX(-1)) : mrb_fixnum_value(INT2FIX(1)));
}


mrb_value
n_mrb_big_cmp(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_cmp(mrb, x, y);
}



static mrb_value
big_op(mrb_state *mrb, mrb_value x, mrb_value y, int op)
{
    mrb_value rel;
    int n;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
        case MRB_TT_BIGNUM:
            rel = mrb_big_cmp(mrb,x, y);
            break;
            
        case MRB_TT_FLOAT:
        {
            double a = y.value.f;
            
            if (isinf(a)) {
                if (a > 0.0) rel = mrb_fixnum_value(INT2FIX(-1));
                else rel = mrb_fixnum_value(INT2FIX(1));
                break;
            }
            rel = mrb_fixnum_value(mrb_dbl_cmp(x.value.f, a));
            break;
        }
            
        default:
        {
            char * id ;
            switch (op) {
                case 0: id = ">"; break;
                case 1: id = ">="; break;
                case 2: id = "<"; break;
                case 3: id = "<="; break;
            }
            
            return mrb_funcall(mrb, x, id, 1,y);
        }
    }
    
    if (mrb_nil_p(rel)) return mrb_fixnum_value(MRB_TT_FALSE);
    n = FIX2LONG(rel.value.i);
    
    switch (op) {
        case 0: return n >  0 ? mrb_fixnum_value(MRB_TT_TRUE) : mrb_fixnum_value(MRB_TT_FALSE);
        case 1: return n >= 0 ? mrb_fixnum_value(MRB_TT_TRUE) : mrb_fixnum_value(MRB_TT_FALSE);
        case 2: return n <  0 ? mrb_fixnum_value(MRB_TT_TRUE) : mrb_fixnum_value(MRB_TT_FALSE);
        case 3: return n <= 0 ? mrb_fixnum_value(MRB_TT_TRUE) : mrb_fixnum_value(MRB_TT_FALSE);
    }
    return mrb_fixnum_value(MRB_TT_UNDEF);
}

/*
 * call-seq:
 *   big > real  ->  true or false
 *
 * Returns <code>true</code> if the mrb_value of <code>big</code> is
 * greater than that of <code>real</code>.
 */

static mrb_value
big_gt(mrb_state *mrb, mrb_value x, mrb_value y)
{
    return big_op(mrb,x, y, 0);
}

mrb_value
n_mrb_big_gt(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return big_gt(mrb, x, y);
}



/*
 * call-seq:
 *   big >= real  ->  true or false
 *
 * Returns <code>true</code> if the mrb_value of <code>big</code> is
 * greater than or equal to that of <code>real</code>.
 */

static mrb_value
big_ge(mrb_state *mrb, mrb_value x, mrb_value y)
{
    return big_op(mrb,x, y, 1);
}

mrb_value
n_mrb_big_ge(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return big_ge(mrb, x, y);
}



/*
 * call-seq:
 *   big < real  ->  true or false
 *
 * Returns <code>true</code> if the mrb_value of <code>big</code> is
 * less than that of <code>real</code>.
 */

static mrb_value
big_lt(mrb_state *mrb, mrb_value x, mrb_value y)
{
    return big_op(mrb,x, y, 2);
}

mrb_value
n_mrb_big_lt(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return big_lt(mrb, x, y);
}



/*
 * call-seq:
 *   big <= real  ->  true or false
 *
 * Returns <code>true</code> if the mrb_value of <code>big</code> is
 * less than or equal to that of <code>real</code>.
 */

static mrb_value
big_le(mrb_state *mrb, mrb_value x, mrb_value y)
{
    return big_op(mrb,x, y, 3);
}

mrb_value
n_mrb_big_le(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return big_le(mrb, x, y);
}



/*
 *  call-seq:
 *     big == obj  -> true or false
 *
 *  Returns <code>true</code> only if <i>obj</i> has the same mrb_value
 *  as <i>big</i>. Contrast this with <code>Bignum#eql?</code>, which
 *  requires <i>obj</i> to be a <code>Bignum</code>.
 *
 *     68719476736 == 68719476736.0   #=> true
 */

mrb_value
mrb_big_eq(mrb_state *mrb, mrb_value x, mrb_value y)
{
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y = mrb_int2big(mrb,mrb_fixnum_value(FIX2LONG(y.value.i)));
            break;
        case MRB_TT_BIGNUM:
            break;
        case MRB_TT_FLOAT:
        {
            volatile double a, b;
            
            a = y.value.f;
            if (isnan(a) || isinf(a)) return mrb_fixnum_value(MRB_TT_FALSE);
            b = mrb_big2dbl(mrb,x);
            return (a == b)?mrb_fixnum_value(MRB_TT_TRUE):mrb_fixnum_value(MRB_TT_FALSE);
        }
        default:
            return mrb_fixnum_value(mrb_equal(mrb,y, x));
    }
    if (mrb_bignum_sign(x) != mrb_bignum_sign(y)) return mrb_fixnum_value(MRB_TT_FALSE);
    if (mrb_bignum_len(x) != mrb_bignum_len(y)) return mrb_fixnum_value(MRB_TT_FALSE);
    if (MEMCMP(mrb_bignum_digits(x),mrb_bignum_digits(y),BDIGIT,mrb_bignum_len(y)) != 0)
        return mrb_fixnum_value(MRB_TT_FALSE);
    return mrb_fixnum_value(MRB_TT_TRUE);
}

mrb_value
n_mrb_big_eq(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_eq(mrb, x, y);
}




/*
 *  call-seq:
 *     big.eql?(obj)   -> true or false
 *
 *  Returns <code>true</code> only if <i>obj</i> is a
 *  <code>Bignum</code> with the same mrb_value as <i>big</i>. Contrast this
 *  with <code>Bignum#==</code>, which performs type conversions.
 *
 *     68719476736.eql?(68719476736.0)   #=> false
 */

static mrb_value
mrb_big_eql(mrb_state *mrb, mrb_value x, mrb_value y)
{
    if (mrb_type(y) != MRB_TT_BIGNUM) return mrb_fixnum_value(MRB_TT_FALSE);
    if (mrb_bignum_sign(x) != mrb_bignum_sign(y)) return mrb_fixnum_value(MRB_TT_FALSE);
    if (mrb_bignum_len(x) != mrb_bignum_len(y)) return mrb_fixnum_value(MRB_TT_FALSE);
    if (MEMCMP(mrb_bignum_digits(x),mrb_bignum_digits(y),BDIGIT,mrb_bignum_len(y)) != 0)
        return mrb_fixnum_value(MRB_TT_FALSE);
    return mrb_fixnum_value(MRB_TT_TRUE);
}

mrb_value
n_mrb_big_eql(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_eql(mrb, x, y);
}



/*
 * call-seq:
 *    -big   ->  integer
 *
 * Unary minus (returns an integer whose mrb_value is 0-big)
 */

mrb_value
mrb_big_uminus(mrb_state *mrb, mrb_value x)
{
    mrb_value z = mrb_big_clone(mrb,x);
    
    mrb_bignum_set_sign(z, !mrb_bignum_sign(x));
    
    return bignorm(mrb,z);
}



/*
 * call-seq:
 *     ~big  ->  integer
 *
 * Inverts the bits in big. As Bignums are conceptually infinite
 * length, the result acts as if it had an infinite number of one
 * bits to the left. In hex representations, this is displayed
 * as two periods to the left of the digits.
 *
 *   sprintf("%X", ~0x1122334455)    #=> "..FEEDDCCBBAA"
 */

static mrb_value
mrb_big_neg(mrb_state *mrb, mrb_value x)
{
    mrb_value z = mrb_big_clone(mrb,x);
    BDIGIT *ds;
    long i;
    
    if (!mrb_bignum_sign(x)) get2comp(mrb,z);
    ds = mrb_bignum_digits(z);
    i = mrb_bignum_len(x);
    if (!i) return mrb_fixnum_value(INT2FIX(~(SIGNED_VALUE)0));
    while (i--) {
        ds[i] = ~ds[i];
    }
    mrb_bignum_set_sign(z, !mrb_bignum_sign(z));
    if (mrb_bignum_sign(x)) get2comp(mrb,z);
    
    return bignorm(mrb,z);
}

static void
bigsub_core(mrb_state *mrb, BDIGIT *xds, long xn, BDIGIT *yds, long yn, BDIGIT *zds, long zn)
{
    BDIGIT_DBL_SIGNED num;
    long i;
    
    for (i = 0, num = 0; i < yn; i++) {
        num += (BDIGIT_DBL_SIGNED)xds[i] - yds[i];
        zds[i] = BIGLO(num);
        num = BIGDN(num);
    }
    while (num && i < xn) {
        num += xds[i];
        zds[i++] = BIGLO(num);
        num = BIGDN(num);
    }
    while (i < xn) {
        zds[i] = xds[i];
        i++;
    }
    assert(i <= zn);
    while (i < zn) {
        zds[i++] = 0;
    }
}

static mrb_value
bigsub(mrb_state *mrb, mrb_value x, mrb_value y)
{
    mrb_value z = mrb_fixnum_value(0);
    long i = mrb_bignum_len(x);
    BDIGIT *xds, *yds;
    
    /* if x is smaller than y, swap */
    if (mrb_bignum_len(x) < mrb_bignum_len(y)) {
        z = x; x = y; y = z;	/* swap x y */
    }
    else if (mrb_bignum_len(x) == mrb_bignum_len(y)) {
        xds = mrb_bignum_digits(x);
        yds = mrb_bignum_digits(y);
        while (i > 0) {
            i--;
            if (xds[i] > yds[i]) {
                break;
            }
            if (xds[i] < yds[i]) {
                z = x; x = y; y = z;	/* swap x y */
                break;
            }
        }
    }
    
    z = mrb_bignum_new(mrb,mrb_bignum_len(x), z.value.i==0);
    bigsub_core(mrb,mrb_bignum_digits(x), mrb_bignum_len(x),
                mrb_bignum_digits(y), mrb_bignum_len(y),
                mrb_bignum_digits(z), mrb_bignum_len(z));
    
    return z;
}

static mrb_value bigadd_int(mrb_state *mrb, mrb_value x, long y);

static mrb_value
bigsub_int(mrb_state *mrb, mrb_value x, long y0)
{
    mrb_value z;
    BDIGIT *xds, *zds;
    long xn;
    BDIGIT_DBL_SIGNED num;
    long i, y;
    
    y = y0;
    xds = mrb_bignum_digits(x);
    xn = mrb_bignum_len(x);
    
    z = mrb_bignum_new(mrb,xn, mrb_bignum_sign(x));
    zds = mrb_bignum_digits(z);
    
#if SIZEOF_mrb_bignum_digits == SIZEOF_LONG
    num = (BDIGIT_DBL_SIGNED)xds[0] - y;
    if (xn == 1 && num < 0) {
        mrb_bignum_set_sign(z, !mrb_bignum_sign(x));
        zds[0] = (BDIGIT)-num;
        RB_GC_GUARD(x);
        return bignorm(mrb,z);
    }
    zds[0] = BIGLO(num);
    num = BIGDN(num);
    i = 1;
#else
    num = 0;
    for (i=0; i<(int)(sizeof(y)/sizeof(BDIGIT)); i++) {
        num += (BDIGIT_DBL_SIGNED)xds[i] - BIGLO(y);
        zds[i] = BIGLO(num);
        num = BIGDN(num);
        y = BIGDN(y);
    }
#endif
    while (num && i < xn) {
        num += xds[i];
        zds[i++] = BIGLO(num);
        num = BIGDN(num);
    }
    while (i < xn) {
        zds[i] = xds[i];
        i++;
    }
    if (num < 0) {
        z = bigsub(mrb,x, mrb_int2big(mrb,mrb_fixnum_value(y0)));
    }
    mrb_gc_mark(mrb,mrb_basic(x));
    return bignorm(mrb,z);
}

static mrb_value
bigadd_int(mrb_state *mrb, mrb_value x, long y)
{
    mrb_value z;
    BDIGIT *xds, *zds;
    long xn, zn;
    BDIGIT_DBL num;
    long i;
    
    xds = mrb_bignum_digits(x);
    xn = mrb_bignum_len(x);
    
    if (xn < 2) {
        zn = 3;
    }
    else {
        zn = xn + 1;
    }
    z = mrb_bignum_new(mrb,zn, mrb_bignum_sign(x));
    zds = mrb_bignum_digits(z);
    
#if SIZEOF_mrb_bignum_digits == SIZEOF_LONG
    num = (BDIGIT_DBL)xds[0] + y;
    zds[0] = BIGLO(num);
    num = BIGDN(num);
    i = 1;
#else
    num = 0;
    for (i=0; i<(int)(sizeof(y)/sizeof(BDIGIT)); i++) {
        num += (BDIGIT_DBL)xds[i] + BIGLO(y);
        zds[i] = BIGLO(num);
        num = BIGDN(num);
        y = BIGDN(y);
    }
#endif
    while (num && i < xn) {
        num += xds[i];
        zds[i++] = BIGLO(num);
        num = BIGDN(num);
    }
    if (num) zds[i++] = (BDIGIT)num;
    else while (i < xn) {
        zds[i] = xds[i];
        i++;
    }
    assert(i <= zn);
    while (i < zn) {
        zds[i++] = 0;
    }
    mrb_gc_mark(mrb, mrb_basic(x));
    return bignorm(mrb,z);
}

static void
bigadd_core(BDIGIT *xds, long xn, BDIGIT *yds, long yn, BDIGIT *zds, long zn)
{
    BDIGIT_DBL num = 0;
    long i;
    
    if (xn > yn) {
        BDIGIT *tds;
        tds = xds; xds = yds; yds = tds;
        i = xn; xn = yn; yn = i;
    }
    
    i = 0;
    while (i < xn) {
        num += (BDIGIT_DBL)xds[i] + yds[i];
        zds[i++] = BIGLO(num);
        num = BIGDN(num);
    }
    while (num && i < yn) {
        num += yds[i];
        zds[i++] = BIGLO(num);
        num = BIGDN(num);
    }
    while (i < yn) {
        zds[i] = yds[i];
        i++;
    }
    if (num) zds[i++] = (BDIGIT)num;
    assert(i <= zn);
    while (i < zn) {
        zds[i++] = 0;
    }
}

static mrb_value
bigadd(mrb_state *mrb, mrb_value x, mrb_value y, int sign)
{
    mrb_value z;
    long len;
    
    sign = (sign == mrb_bignum_sign(y));
    if (mrb_bignum_sign(x) != sign) {
        if (sign) return bigsub(mrb,y, x);
        return bigsub(mrb,x, y);
    }
    
    if (mrb_bignum_len(x) > mrb_bignum_len(y)) {
        len = mrb_bignum_len(x) + 1;
    }
    else {
        len = mrb_bignum_len(y) + 1;
    }
    z = mrb_bignum_new(mrb,len, sign);
    
    bigadd_core(mrb_bignum_digits(x), mrb_bignum_len(x),
                mrb_bignum_digits(y), mrb_bignum_len(y),
                mrb_bignum_digits(z), mrb_bignum_len(z));
    
    return z;
}

/*
 *  call-seq:
 *     big + other  -> Numeric
 *
 *  Adds big and other, returning the result.
 */

mrb_value
mrb_big_plus(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long n;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            n = FIX2LONG(y.value.i);
            if ((n > 0) != mrb_bignum_sign(x)) {
                if (n < 0) {
                    n = -n;
                }
                return bigsub_int(mrb,x, n);
            }
            if (n < 0) {
                n = -n;
            }
            return bigadd_int(mrb,x, n);
            
        case MRB_TT_BIGNUM:
            return bignorm(mrb,bigadd(mrb,x, y, 1));
            
        case MRB_TT_FLOAT:
            return mrb_fixnum_value(mrb_big2dbl(mrb,x) + y.value.f);
            
        default:
            return mrb_funcall(mrb, x, "+",1,y);
    }
}

mrb_value
n_mrb_big_plus(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_plus(mrb, x, y);
}

/*
 *  call-seq:
 *     big - other  -> Numeric
 *
 *  Subtracts other from big, returning the result.
 */

mrb_value
mrb_big_minus(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long n;
    double dv;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            n = FIX2LONG(y.value.i);
            if ((n > 0) != mrb_bignum_sign(x)) {
                if (n < 0) {
                    n = -n;
                }
                return bigadd_int(mrb,x, n);
            }
            if (n < 0) {
                n = -n;
            }
            return bigsub_int(mrb,x, n);
            
        case MRB_TT_BIGNUM:
            return bignorm(mrb,bigadd(mrb,x, y, 0));
            
        case MRB_TT_FLOAT:
             dv = mrb_big2dbl(mrb,x) - y.value.f;
            return mrb_float_new(mrb,dv);
            
        default:
            return mrb_funcall(mrb, x, "-", 1, y);
    }
}

mrb_value
n_mrb_big_minus(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_minus(mrb, x, y);
}


static long
big_real_len(mrb_value x)
{
    long i = mrb_bignum_len(x);
    BDIGIT *xds = mrb_bignum_digits(x);
    while (--i && !xds[i]);
    return i + 1;
}

static mrb_value 
bigmul1_single(mrb_state *mrb, mrb_value x, mrb_value y)
{
    BDIGIT_DBL n;
    mrb_value z = mrb_bignum_new(mrb,2, mrb_bignum_sign(x)==mrb_bignum_sign(y));
    BDIGIT *xds, *yds, *zds;
    
    xds = mrb_bignum_digits(x);
    yds = mrb_bignum_digits(y);
    zds = mrb_bignum_digits(z);
    
    n = (BDIGIT_DBL)xds[0] * yds[0];
    zds[0] = BIGLO(n);
    zds[1] = (BDIGIT)BIGDN(n);
    
    return z;
}

static mrb_value
bigmul1_normal(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long xl = mrb_bignum_len(x), yl = mrb_bignum_len(y), i, j = xl + yl + 1;
    BDIGIT_DBL n = 0;
    mrb_value z = mrb_bignum_new(mrb,j, mrb_bignum_sign(x)==mrb_bignum_sign(y));
    BDIGIT *xds, *yds, *zds;
    
    xds = mrb_bignum_digits(x);
    yds = mrb_bignum_digits(y);
    zds = mrb_bignum_digits(z);
    while (j--) zds[j] = 0;
    for (i = 0; i < xl; i++) {
        BDIGIT_DBL dd;
        dd = xds[i];
        if (dd == 0) continue;
        n = 0;
        for (j = 0; j < yl; j++) {
            BDIGIT_DBL ee = n + (BDIGIT_DBL)dd * yds[j];
            n = zds[i + j] + ee;
            if (ee) zds[i + j] = BIGLO(n);
            n = BIGDN(n);
        }
        if (n) {
            zds[i + j] = (BDIGIT)n;
        }
    }
    //rb_thread_check_ints();
    return z;
}


static mrb_value bigmul0(mrb_state *mrb, mrb_value x, mrb_value y);

/* balancing multiplication by slicing larger argument */
static mrb_value
bigmul1_balance(mrb_state *mrb, mrb_value x, mrb_value y)
{
    mrb_value z, t1, t2;
    long i, xn, yn, r, n;
    BDIGIT *yds, *zds, *t1ds;
    
    xn = mrb_bignum_len(x);
    yn = mrb_bignum_len(y);
    assert(2 * xn <= yn || 3 * xn <= 2*(yn+2));
    
    z = mrb_bignum_new(mrb,xn + yn, mrb_bignum_sign(x)==mrb_bignum_sign(y));
    t1 = mrb_bignum_new(mrb,xn, 1);
    
    yds = mrb_bignum_digits(y);
    zds = mrb_bignum_digits(z);
    t1ds = mrb_bignum_digits(t1);
    
    for (i = 0; i < xn + yn; i++) zds[i] = 0;
    
    n = 0;
    while (yn > 0) {
        r = xn > yn ? yn : xn;
        mrb_bignum_movein(t1, yds + n, r);
        mrb_bignum_set_len(t1, r);
        t2 = bigmul0(mrb,x, t1);
        bigadd_core(zds + n, mrb_bignum_len(z) - n,
                    mrb_bignum_digits(t2), big_real_len(t2),
                    zds + n, mrb_bignum_len(z) - n);
        yn -= r;
        n += r;
    }
    
    return z;
}

/* split a bignum into high and low bignums */
static void
big_split(mrb_state *mrb, mrb_value v, long n, volatile mrb_value *ph, volatile mrb_value *pl)
{
    long hn = 0, ln = mrb_bignum_len(v);
    mrb_value h, l;
    BDIGIT *vds = mrb_bignum_digits(v);
    
    if (ln > n) {
        hn = ln - n;
        ln = n;
    }
    
    if (!hn) {
        h = mrb_uint2big(mrb,mrb_fixnum_value(0));
    }
    else {
        while (--hn && !vds[hn + ln]);
        h = mrb_bignum_new(mrb,hn += 2, 1);
        MEMCPY(mrb_bignum_digits(h), vds + ln, BDIGIT, hn - 1);
        mrb_bignum_digits(h)[hn - 1] = 0; /* margin for carry */
    }
    
    while (--ln && !vds[ln]);
    l = mrb_bignum_new(mrb,ln += 2, 1);
    MEMCPY(mrb_bignum_digits(l), vds, BDIGIT, ln - 1);
    mrb_bignum_digits(l)[ln - 1] = 0; /* margin for carry */
    
    *pl = l;
    *ph = h;
}

/* multiplication by karatsuba method */
static mrb_value
bigmul1_karatsuba(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long i, n, xn, yn, t1n, t2n;
    mrb_value xh, xl, yh, yl, z, t1, t2, t3;
    BDIGIT *zds;
    
    xn = mrb_bignum_len(x);
    yn = mrb_bignum_len(y);
    n = yn / 2;
    big_split(mrb,x, n, &xh, &xl);
    if (x.value.i == y.value.i) {
        yh = xh; yl = xl;
    }
    else big_split(mrb,y, n, &yh, &yl);
    
    /* x = xh * b + xl
     * y = yh * b + yl
     *
     * Karatsuba method:
     *   x * y = z2 * b^2 + z1 * b + z0
     *   where
     *     z2 = xh * yh
     *     z0 = xl * yl
     *     z1 = (xh + xl) * (yh + yl) - z2 - z0
     *
     *  ref: http://en.wikipedia.org/wiki/Karatsuba_algorithm
     */
    
    /* allocate a result bignum */
    z = mrb_bignum_new(mrb,xn + yn, mrb_bignum_sign(x)==mrb_bignum_sign(y));
    zds = mrb_bignum_digits(z);
    
    /* t1 <- xh * yh */
    t1 = bigmul0(mrb,xh, yh);
    t1n = big_real_len(t1);
    
    /* copy t1 into high bytes of the result (z2) */
    MEMCPY(zds + 2 * n, mrb_bignum_digits(t1), BDIGIT, t1n);
    for (i = 2 * n + t1n; i < xn + yn; i++) zds[i] = 0;
    
    if (!BIGZEROP(xl) && !BIGZEROP(yl)) {
        /* t2 <- xl * yl */
        t2 = bigmul0(mrb,xl, yl);
        t2n = big_real_len(t2);
        
        /* copy t2 into low bytes of the result (z0) */
        MEMCPY(zds, mrb_bignum_digits(t2), BDIGIT, t2n);
        for (i = t2n; i < 2 * n; i++) zds[i] = 0;
    }
    else {
        t2 = mrb_fixnum_value(MRB_TT_UNDEF);
        t2n = 0;
        
        /* copy 0 into low bytes of the result (z0) */
        for (i = 0; i < 2 * n; i++) zds[i] = 0;
    }
    
    /* xh <- xh + xl */
    if (mrb_bignum_len(xl) > mrb_bignum_len(xh)) {
        t3 = xl; xl = xh; xh = t3;
    }
    /* xh has a margin for carry */
    bigadd_core(mrb_bignum_digits(xh), mrb_bignum_len(xh),
                mrb_bignum_digits(xl), mrb_bignum_len(xl),
                mrb_bignum_digits(xh), mrb_bignum_len(xh));
    
    /* yh <- yh + yl */
    if (x.value.i != y.value.i) {
        if (mrb_bignum_len(yl) > mrb_bignum_len(yh)) {
            t3 = yl; yl = yh; yh = t3;
        }
        /* yh has a margin for carry */
        bigadd_core(mrb_bignum_digits(yh), mrb_bignum_len(yh),
                    mrb_bignum_digits(yl), mrb_bignum_len(yl),
                    mrb_bignum_digits(yh), mrb_bignum_len(yh));
    }
    else yh = xh;
    
    /* t3 <- xh * yh */
    t3 = bigmul0(mrb,xh, yh);
    
    i = xn + yn - n;
    /* subtract t1 from t3 */
    bigsub_core(mrb,mrb_bignum_digits(t3), big_real_len(t3), mrb_bignum_digits(t1), t1n, mrb_bignum_digits(t3), big_real_len(t3));
    
    /* subtract t2 from t3; t3 is now the middle term of the product */
    if (t2.value.i != MRB_TT_UNDEF)
        bigsub_core(mrb,mrb_bignum_digits(t3), big_real_len(t3), mrb_bignum_digits(t2), t2n, mrb_bignum_digits(t3), big_real_len(t3));
    
    /* add t3 to middle bytes of the result (z1) */
    bigadd_core(zds + n, i, mrb_bignum_digits(t3), big_real_len(t3), zds + n, i);
    
    return z;
}

static void
biglsh_bang(mrb_state *mrb, BDIGIT *xds, long xn, unsigned long shift)
{
    long const s1 = shift/BITSPERDIG;
    int const s2 = (int)(shift%BITSPERDIG);
    int const s3 = BITSPERDIG-s2;
    BDIGIT* zds;
    BDIGIT num;
    long i;
    if (s1 >= xn) {
        MEMZERO(xds, BDIGIT, xn);
        return;
    }
    zds = xds + xn - 1;
    xn -= s1 + 1;
    num = xds[xn]<<s2;
    do {
        *zds-- = num | xds[--xn]>>s3;
        num = xds[xn]<<s2;
    }
    while (xn > 0);
    *zds = num;
    for (i = s1; i > 0; --i)
        *zds-- = 0;
}

static void
bigrsh_bang(mrb_state *mrb, BDIGIT* xds, long xn, unsigned long shift)
{
    long s1 = shift/BITSPERDIG;
    int s2 = (int)(shift%BITSPERDIG);
    int s3 = BITSPERDIG - s2;
    int i;
    BDIGIT num;
    BDIGIT* zds;
    if (s1 >= xn) {
        MEMZERO(xds, BDIGIT, xn);
        return;
    }
    
    i = 0;
    zds = xds + s1;
    num = *zds++>>s2;
    do {
        xds[i++] = (BDIGIT)(*zds<<s3) | num;
        num = *zds++>>s2;
    }
    while (i < xn - s1 - 1);
    xds[i] = num;
    MEMZERO(xds + xn - s1, BDIGIT, s1);
}

static void
big_split3(mrb_state *mrb, mrb_value v, long n, volatile mrb_value* p0, volatile mrb_value* p1, volatile mrb_value* p2)
{
    mrb_value v0, v12, v1, v2;
    
    big_split(mrb,v, n, &v12, &v0);
    big_split(mrb,v12, n, &v2, &v1);
    
    *p0 = bigtrunc(mrb,v0);
    *p1 = bigtrunc(mrb,v1);
    *p2 = bigtrunc(mrb,v2);
}

static mrb_value big_lshift(mrb_state *,mrb_value, unsigned long);
static mrb_value big_rshift(mrb_state *,mrb_value, unsigned long);
static mrb_value bigdivrem(mrb_state *,mrb_value, mrb_value, volatile mrb_value*, volatile mrb_value*);

static mrb_value
bigmul1_toom3(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long n, xn, yn, zn;
    mrb_value x0, x1, x2, y0, y1, y2;
    mrb_value u0, u1, u2, u3, u4, v1, v2, v3;
    mrb_value z0, z1, z2, z3, z4, z, t;
    BDIGIT* zds;
    
    xn = mrb_bignum_len(x);
    yn = mrb_bignum_len(y);
    assert(xn <= yn);  /* assume y >= x */
    
    n = (yn + 2) / 3;
    big_split3(mrb,x, n, &x0, &x1, &x2);
    if (x.value.i == y.value.i) {
        y0 = x0; y1 = x1; y2 = x2;
    }
    else big_split3(mrb,y, n, &y0, &y1, &y2);
    
    /*
     * ref. http://en.wikipedia.org/wiki/Toom%E2%80%93Cook_multiplication
     *
     * x(b) = x0 * b^0 + x1 * b^1 + x2 * b^2
     * y(b) = y0 * b^0 + y1 * b^1 + y2 * b^2
     *
     * z(b) = x(b) * y(b)
     * z(b) = z0 * b^0 + z1 * b^1 + z2 * b^2 + z3 * b^3 + z4 * b^4
     * where:
     *   z0 = x0 * y0
     *   z1 = x0 * y1 + x1 * y0
     *   z2 = x0 * y2 + x1 * y1 + x2 * y0
     *   z3 = x1 * y2 + x2 * y1
     *   z4 = x2 * y2
     *
     * Toom3 method (a.k.a. Toom-Cook method):
     * (Step1) calculating 5 points z(b0), z(b1), z(b2), z(b3), z(b4),
     * where:
     *   b0 = 0, b1 = 1, b2 = -1, b3 = -2, b4 = inf,
     *   z(0)   = x(0)   * y(0)   = x0 * y0
     *   z(1)   = x(1)   * y(1)   = (x0 + x1 + x2) * (y0 + y1 + y2)
     *   z(-1)  = x(-1)  * y(-1)  = (x0 - x1 + x2) * (y0 - y1 + y2)
     *   z(-2)  = x(-2)  * y(-2)  = (x0 - 2 * (x1 - 2 * x2)) * (y0 - 2 * (y1 - 2 * y2))
     *   z(inf) = x(inf) * y(inf) = x2 * y2
     *
     * (Step2) interpolating z0, z1, z2, z3, z4, and z5.
     *
     * (Step3) Substituting base mrb_value into b of the polynomial z(b),
     */
    
    /*
     * [Step1] calculating 5 points z(b0), z(b1), z(b2), z(b3), z(b4)
     */
    
    /* u1 <- x0 + x2 */
    u1 = bigtrunc(mrb,bigadd(mrb,x0, x2, 1));
    
    /* x(-1) : u2 <- u1 - x1 = x0 - x1 + x2 */
    u2 = bigtrunc(mrb,bigsub(mrb,u1, x1));
    
    /* x(1) : u1 <- u1 + x1 = x0 + x1 + x2 */
    u1 = bigtrunc(mrb,bigadd(mrb,u1, x1, 1));
    
    /* x(-2) : u3 <- 2 * (u2 + x2) - x0 = x0 - 2 * (x1 - 2 * x2) */
    u3 = bigadd(mrb,u2, x2, 1);
    if (mrb_bignum_digits(u3)[mrb_bignum_len(u3)-1] & BIGRAD_HALF) {
        mrb_big_resize(mrb,u3, mrb_bignum_len(u3) + 1);
        mrb_bignum_digits(u3)[mrb_bignum_len(u3)-1] = 0;
    }
    biglsh_bang(mrb,mrb_bignum_digits(u3), mrb_bignum_len(u3), 1);
    u3 = bigtrunc(mrb,bigadd(mrb,bigtrunc(mrb,u3), x0, 0));
    
    if (x.value.i == y.value.i) {
        v1 = u1; v2 = u2; v3 = u3;
    }
    else {
        /* v1 <- y0 + y2 */
        v1 = bigtrunc(mrb,bigadd(mrb,y0, y2, 1));
        
        /* y(-1) : v2 <- v1 - y1 = y0 - y1 + y2 */
        v2 = bigtrunc(mrb,bigsub(mrb,v1, y1));
        
        /* y(1) : v1 <- v1 + y1 = y0 + y1 + y2 */
        v1 = bigtrunc(mrb,bigadd(mrb,v1, y1, 1));
        
        /* y(-2) : v3 <- 2 * (v2 + y2) - y0 = y0 - 2 * (y1 - 2 * y2) */
        v3 = bigadd(mrb,v2, y2, 1);
        if (mrb_bignum_digits(v3)[mrb_bignum_len(v3)-1] & BIGRAD_HALF) {
            mrb_big_resize(mrb,v3, mrb_bignum_len(v3) + 1);
            mrb_bignum_digits(v3)[mrb_bignum_len(v3)-1] = 0;
        }
        biglsh_bang(mrb,mrb_bignum_digits(v3), mrb_bignum_len(v3), 1);
        v3 = bigtrunc(mrb,bigadd(mrb,bigtrunc(mrb,v3), y0, 0));
    }
    
    /* z(0) : u0 <- x0 * y0 */
    u0 = bigtrunc(mrb,bigmul0(mrb,x0, y0));
    
    /* z(1) : u1 <- u1 * v1 */
    u1 = bigtrunc(mrb,bigmul0(mrb,u1, v1));
    
    /* z(-1) : u2 <- u2 * v2 */
    u2 = bigtrunc(mrb,bigmul0(mrb,u2, v2));
    
    /* z(-2) : u3 <- u3 * v3 */
    u3 = bigtrunc(mrb,bigmul0(mrb,u3, v3));
    
    /* z(inf) : u4 <- x2 * y2 */
    u4 = bigtrunc(mrb,bigmul0(mrb,x2, y2));
    
    /* for GC */
    v1 = v2 = v3 = mrb_fixnum_value(MRB_TT_NIL);
    
    /*
     * [Step2] interpolating z0, z1, z2, z3, z4, and z5.
     */
    
    /* z0 <- z(0) == u0 */
    z0 = u0;
    
    /* z4 <- z(inf) == u4 */
    z4 = u4;
    
    /* z3 <- (z(-2) - z(1)) / 3 == (u3 - u1) / 3 */
    z3 = bigadd(mrb,u3, u1, 0);
    bigdivrem(mrb,z3, big_three, &z3, NULL); /* TODO: optimize */
    bigtrunc(mrb,z3);
    
    /* z1 <- (z(1) - z(-1)) / 2 == (u1 - u2) / 2 */
    z1 = bigtrunc(mrb,bigadd(mrb,u1, u2, 0));
    bigrsh_bang(mrb,mrb_bignum_digits(z1), mrb_bignum_len(z1), 1);
    
    /* z2 <- z(-1) - z(0) == u2 - u0 */
    z2 = bigtrunc(mrb,bigadd(mrb,u2, u0, 0));
    
    /* z3 <- (z2 - z3) / 2 + 2 * z(inf) == (z2 - z3) / 2 + 2 * u4 */
    z3 = bigtrunc(mrb,bigadd(mrb,z2, z3, 0));
    bigrsh_bang(mrb,mrb_bignum_digits(z3), mrb_bignum_len(z3), 1);
    t = big_lshift(mrb,u4, 1); /* TODO: combining with next addition */
    z3 = bigtrunc(mrb,bigadd(mrb,z3, t, 1));
    
    /* z2 <- z2 + z1 - z(inf) == z2 + z1 - u4 */
    z2 = bigtrunc(mrb,bigadd(mrb,z2, z1, 1));
    z2 = bigtrunc(mrb,bigadd(mrb,z2, u4, 0));
    
    /* z1 <- z1 - z3 */
    z1 = bigtrunc(mrb,bigadd(mrb,z1, z3, 0));
    
    /*
     * [Step3] Substituting base mrb_value into b of the polynomial z(b),
     */
    
    zn = 6*n + 1;
    z = mrb_bignum_new(mrb,zn, mrb_bignum_sign(x)==mrb_bignum_sign(y));
    zds = mrb_bignum_digits(z);
    MEMCPY(zds, mrb_bignum_digits(z0), BDIGIT, mrb_bignum_len(z0));
    MEMZERO(zds + mrb_bignum_len(z0), BDIGIT, zn - mrb_bignum_len(z0));
    bigadd_core(zds +   n, zn -   n, mrb_bignum_digits(z1), big_real_len(z1), zds +   n, zn -   n);
    bigadd_core(zds + 2*n, zn - 2*n, mrb_bignum_digits(z2), big_real_len(z2), zds + 2*n, zn - 2*n);
    bigadd_core(zds + 3*n, zn - 3*n, mrb_bignum_digits(z3), big_real_len(z3), zds + 3*n, zn - 3*n);
    bigadd_core(zds + 4*n, zn - 4*n, mrb_bignum_digits(z4), big_real_len(z4), zds + 4*n, zn - 4*n);
    z = bignorm(mrb,z);
    
    return bignorm(mrb,z);
}

/* efficient squaring (2 times faster than normal multiplication)
 * ref: Handbook of Applied Cryptography, Algorithm 14.16
 *      http://www.cacr.math.uwaterloo.ca/hac/about/chap14.pdf
 */
static mrb_value
bigsqr_fast(mrb_state *mrb, mrb_value x)
{
    long len = mrb_bignum_len(x), i, j;
    mrb_value z = mrb_bignum_new(mrb,2 * len + 1, 1);
    BDIGIT *xds = mrb_bignum_digits(x), *zds = mrb_bignum_digits(z);
    BDIGIT_DBL c, v, w;
    
    for (i = 2 * len + 1; i--; ) zds[i] = 0;
    for (i = 0; i < len; i++) {
        v = (BDIGIT_DBL)xds[i];
        if (!v) continue;
        c = (BDIGIT_DBL)zds[i + i] + v * v;
        zds[i + i] = BIGLO(c);
        c = BIGDN(c);
        v *= 2;
        for (j = i + 1; j < len; j++) {
            w = (BDIGIT_DBL)xds[j];
            c += (BDIGIT_DBL)zds[i + j] + BIGLO(v) * w;
            zds[i + j] = BIGLO(c);
            c = BIGDN(c);
            if (BIGDN(v)) c += w;
        }
        if (c) {
            c += (BDIGIT_DBL)zds[i + len];
            zds[i + len] = BIGLO(c);
            c = BIGDN(c);
        }
        if (c) zds[i + len + 1] += (BDIGIT)c;
    }
    return z;
}

#define KARATSUBA_MUL_DIGITS 70
#define TOOM3_MUL_DIGITS 150

unsigned long
truerand()
{
    return (unsigned long) rand();
}

unsigned long
ntruerand(unsigned long n)
{
	unsigned long m, r;
    
	/*
	 * set m to the one less than the maximum multiple of n <= 2^32,
	 * so we want a random number <= m.
	 */
	if(n > (1UL<<31))
		m = n-1;
	else
    /* 2^32 - 2^32%n - 1 = (2^32 - 1) - (2*(2^31%n))%n */
		m = 0xFFFFFFFFUL - (2*((1UL<<31)%n))%n;
    
	while((r = truerand()) > m)
		;
    
	return r%n;
}

unsigned long  mrb_genrand_ulong_limited(unsigned long  n) {
    unsigned long  res = truerand();
    return res;
}

/* determine whether a bignum is sparse or not by random sampling */
static inline mrb_value
big_sparse_p(mrb_state *mrb, mrb_value x)
{
    long c = 0, n = mrb_bignum_len(x);
    
    if (          mrb_bignum_digits(x)[mrb_genrand_ulong_limited(n / 2) + n / 4]) c++;
    if (c <= 1 && mrb_bignum_digits(x)[mrb_genrand_ulong_limited(n / 2) + n / 4]) c++;
    if (c <= 1 && mrb_bignum_digits(x)[mrb_genrand_ulong_limited(n / 2) + n / 4]) c++;
    
    return (c <= 1) ? mrb_fixnum_value(MRB_TT_TRUE) : mrb_fixnum_value(MRB_TT_FALSE);
}

static mrb_value
bigmul0(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long xn, yn;
    
    xn = mrb_bignum_len(x);
    yn = mrb_bignum_len(y);
    
    /* make sure that y is longer than x */
    if (xn > yn) {
        mrb_value t;
        long tn;
        t = x; x = y; y = t;
        tn = xn; xn = yn; yn = tn;
    }
    assert(xn <= yn);
    
    /* normal multiplication when x is small */
    if (xn < KARATSUBA_MUL_DIGITS) {
    normal:
        if (x.value.i == y.value.i) return bigsqr_fast(mrb,x);
        if (xn == 1 && yn == 1) return bigmul1_single(mrb,x, y);
        return bigmul1_normal(mrb,x, y);
    }
    
    /* normal multiplication when x or y is a sparse bignum */
    if (mrb_test(big_sparse_p(mrb,x))) goto normal;
    if (mrb_test(big_sparse_p(mrb,y))) return bigmul1_normal(mrb,y, x);
    
    /* balance multiplication by slicing y when x is much smaller than y */
    if (2 * xn <= yn) return bigmul1_balance(mrb,x, y);
    
    if (xn < TOOM3_MUL_DIGITS) {
        /* multiplication by karatsuba method */
        return bigmul1_karatsuba(mrb,x, y);
    }
    else if (3*xn <= 2*(yn + 2))
        return bigmul1_balance(mrb,x, y);
    return bigmul1_toom3(mrb,x, y);
}

/*
 *  call-seq:
 *     big * other  -> Numeric
 *
 *  Multiplies big and other, returning the result.
 */

mrb_value
mrb_big_mul(mrb_state *mrb, mrb_value x, mrb_value y)
{
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y = mrb_int2big(mrb,y);
            break;
            
        case MRB_TT_BIGNUM:
            break;
            
        case MRB_TT_FLOAT:
            return mrb_float_new(mrb, mrb_big2dbl(mrb,x) * y.value.f);
            
        default:
            return mrb_funcall(mrb, x, "*", 1, y);
    }
    
    return bignorm(mrb,bigmul0(mrb,x, y));
}


mrb_value
n_mrb_big_mul(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_mul(mrb, x, y);
}

struct big_div_struct {
    long nx, ny;
    BDIGIT *yds, *zds;
    mrb_value stop;
};

static mrb_value
bigdivrem1(mrb_state *mrb, void *ptr)
{
    struct big_div_struct *bds = (struct big_div_struct*)ptr;
    long nx = bds->nx, ny = bds->ny;
    long i, j, nyzero;
    BDIGIT *yds = bds->yds, *zds = bds->zds;
    BDIGIT_DBL t2;
    BDIGIT_DBL_SIGNED num;
    BDIGIT q;
    
    j = nx==ny?nx+1:nx;
    for (nyzero = 0; !yds[nyzero]; nyzero++);
    do {
        if (mrb_test(bds->stop)) return mrb_fixnum_value(MRB_TT_NIL);
        if (zds[j] ==  yds[ny-1]) q = (BDIGIT)BIGRAD-1;
        else q = (BDIGIT)((BIGUP(zds[j]) + zds[j-1])/yds[ny-1]);
        if (q) {
            i = nyzero; num = 0; t2 = 0;
            do {			/* multiply and subtract */
                BDIGIT_DBL ee;
                t2 += (BDIGIT_DBL)yds[i] * q;
                ee = num - BIGLO(t2);
                num = (BDIGIT_DBL)zds[j - ny + i] + ee;
                if (ee) zds[j - ny + i] = BIGLO(num);
                num = BIGDN(num);
                t2 = BIGDN(t2);
            } while (++i < ny);
            num += zds[j - ny + i] - t2;/* borrow from high digit; don't update */
            while (num) {		/* "add back" required */
                i = 0; num = 0; q--;
                do {
                    BDIGIT_DBL ee = num + yds[i];
                    num = (BDIGIT_DBL)zds[j - ny + i] + ee;
                    if (ee) zds[j - ny + i] = BIGLO(num);
                    num = BIGDN(num);
                } while (++i < ny);
                num--;
            }
        }
        zds[j] = q;
    } while (--j >= ny);
    return mrb_fixnum_value(MRB_TT_NIL);
}

static void
mrb_big_stop(mrb_state *mrb, void *ptr)
{
    mrb_value *stop = (mrb_value*)ptr;
    *stop = mrb_fixnum_value(MRB_TT_TRUE);
}

static mrb_value
bigdivrem(mrb_state *mrb, mrb_value x, mrb_value y, volatile mrb_value *divp, volatile mrb_value *modp)
{
    struct big_div_struct bds;
    long nx = mrb_bignum_len(x), ny = mrb_bignum_len(y);
    long i, j;
    mrb_value z, yy, zz;
    BDIGIT *xds, *yds, *zds, *tds;
    BDIGIT_DBL t2;
    BDIGIT dd, q;
    
    if (BIGZEROP(y)) mrb_raise(mrb, mrb->bignum_class, "Bignum division by zero");
    xds = mrb_bignum_digits(x);
    yds = mrb_bignum_digits(y);
    if (nx < ny || (nx == ny && xds[nx - 1] < yds[ny - 1])) {
        if (divp) *divp = mrb_int2big(mrb,mrb_fixnum_value(0));
        if (modp) *modp = x;
        return mrb_fixnum_value(MRB_TT_NIL);
    }
    if (ny == 1) {
        dd = yds[0];
        z = mrb_big_clone(mrb,x);
        zds = mrb_bignum_digits(z);
        t2 = 0; i = nx;
        while (i--) {
            t2 = BIGUP(t2) + zds[i];
            zds[i] = (BDIGIT)(t2 / dd);
            t2 %= dd;
        }
        mrb_bignum_set_sign(z, mrb_bignum_sign(x)==mrb_bignum_sign(y));
        if (modp) {
            *modp = mrb_uint2big(mrb,mrb_fixnum_value(t2));
            mrb_bignum_set_sign(*modp, mrb_bignum_sign(x));
        }
        if (divp) *divp = z;
        return mrb_fixnum_value(MRB_TT_NIL);
    }
    
    z = mrb_bignum_new(mrb,nx==ny?nx+2:nx+1, mrb_bignum_sign(x)==mrb_bignum_sign(y));
    zds = mrb_bignum_digits(z);
    if (nx==ny) zds[nx+1] = 0;
    while (!yds[ny-1]) ny--;
    
    dd = 0;
    q = yds[ny-1];
    while ((q & (BDIGIT)(1UL<<(BITSPERDIG-1))) == 0) {
        q <<= 1UL;
        dd++;
    }
    if (dd) {
        yy = mrb_big_clone(mrb,y);
        tds = mrb_bignum_digits(yy);
        j = 0;
        t2 = 0;
        while (j<ny) {
            t2 += (BDIGIT_DBL)yds[j]<<dd;
            tds[j++] = BIGLO(t2);
            t2 = BIGDN(t2);
        }
        yds = tds;
        RB_GC_GUARD(y) = yy;
        j = 0;
        t2 = 0;
        while (j<nx) {
            t2 += (BDIGIT_DBL)xds[j]<<dd;
            zds[j++] = BIGLO(t2);
            t2 = BIGDN(t2);
        }
        zds[j] = (BDIGIT)t2;
    }
    else {
        zds[nx] = 0;
        j = nx;
        while (j--) zds[j] = xds[j];
    }
    
    bds.nx = nx;
    bds.ny = ny;
    bds.zds = zds;
    bds.yds = yds;
    bds.stop = mrb_fixnum_value(MRB_TT_FALSE);
    bigdivrem1(mrb,&bds);
    
    if (divp) {			/* move quotient down in z */
        *divp = zz = mrb_big_clone(mrb,z);
        zds = mrb_bignum_digits(zz);
        j = (nx==ny ? nx+2 : nx+1) - ny;
        for (i = 0;i < j;i++) zds[i] = zds[i+ny];
        if (!zds[i-1]) i--;
        mrb_bignum_set_len(zz, i);
    }
    if (modp) {			/* normalize remainder */
        *modp = zz = mrb_big_clone(mrb,z);
        zds = mrb_bignum_digits(zz);
        while (--ny && !zds[ny]); ++ny;
        if (dd) {
            t2 = 0; i = ny;
            while(i--) {
                t2 = (t2 | zds[i]) >> dd;
                q = zds[i];
                zds[i] = BIGLO(t2);
                t2 = BIGUP(q);
            }
        }
        if (!zds[ny-1]) ny--;
       
        mrb_bignum_set_len(zz, ny);
        mrb_bignum_set_sign(zz, mrb_bignum_sign(x));
    }
    return z;
}

static void
bigdivmod(mrb_state *mrb, mrb_value x, mrb_value y, volatile mrb_value *divp, volatile mrb_value *modp)
{
    mrb_value mod;
    
    bigdivrem(mrb,x, y, divp, &mod);
    if (mrb_bignum_sign(x) != mrb_bignum_sign(y) && !BIGZEROP(mod)) {
        if (divp) *divp = bigadd(mrb,*divp, mrb_int2big(mrb,mrb_fixnum_value(1)), 0);
        if (modp) *modp = bigadd(mrb,mod, y, 1);
    }
    else if (modp) {
        *modp = mod;
    }
}


static mrb_value
mrb_big_divide(mrb_state *mrb, mrb_value x, mrb_value y, char *op)
{
    mrb_value z;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y = mrb_int2big(mrb,y);
            break;
            
        case MRB_TT_BIGNUM:
            break;
            
        case MRB_TT_FLOAT:
        {
            double div = mrb_big2dbl(mrb,x) / y.value.f;
            if (op[0] == '/') {
                return mrb_float_new(mrb, div);
            }
            else {
                return mrb_dbl2big(mrb,div);
            }
        }
            
        default:
            return mrb_funcall(mrb, x, op, 1, y);
    }
    bigdivmod(mrb,x, y, &z, 0);
    
    return bignorm(mrb,z);
}

/*
 *  call-seq:
 *     big / other     -> Numeric
 *
 * Performs division: the class of the resulting object depends on
 * the class of <code>numeric</code> and on the magnitude of the
 * result.
 */

mrb_value
mrb_big_div(mrb_state *mrb, mrb_value x, mrb_value y)
{
    return mrb_big_divide(mrb,x, y, "/");
}


mrb_value
n_mrb_big_div(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_div(mrb, x, y);
}

/*
 *  call-seq:
 *     big.div(other)  -> integer
 *
 * Performs integer division: returns integer mrb_value.
 */

mrb_value
mrb_big_idiv(mrb_state *mrb, mrb_value x, mrb_value y)
{
    return mrb_big_divide(mrb,x, y, "div");
}

/*
 *  call-seq:
 *     big % other         -> Numeric
 *     big.modulo(other)   -> Numeric
 *
 *  Returns big modulo other. See Numeric.divmod for more
 *  information.
 */

mrb_value
mrb_big_modulo(mrb_state *mrb, mrb_value x, mrb_value y)
{
    mrb_value z;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y = mrb_int2big(mrb,y);
            break;
            
        case MRB_TT_BIGNUM:
            break;
            
        default:
            return mrb_funcall(mrb,x,"%",1,y);
    }
    bigdivmod(mrb,x, y, 0, &z);
    
    return bignorm(mrb,z);
}



mrb_value
n_mrb_big_modulo(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_modulo(mrb, x, y);
}

/*
 *  call-seq:
 *     big.remainder(numeric)    -> number
 *
 *  Returns the remainder after dividing <i>big</i> by <i>numeric</i>.
 *
 *     -1234567890987654321.remainder(13731)      #=> -6966
 *     -1234567890987654321.remainder(13731.24)   #=> -9906.22531493148
 */
static mrb_value
mrb_big_remainder(mrb_state *mrb, mrb_value x, mrb_value y)
{
    mrb_value z;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y = mrb_int2big(mrb,y);
            break;
            
        case MRB_TT_BIGNUM:
            break;
            
        default:
            return mrb_funcall(mrb, x, "remainder",1, y);
    }
    bigdivrem(mrb,x, y, 0, &z);
    
    return bignorm(mrb,z);
}

mrb_value
n_mrb_big_remainder(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_remainder(mrb, x, y);
}



/*
 *  call-seq:
 *     big.divmod(numeric)   -> array
 *
 *  See <code>Numeric#divmod</code>.
 *
 */
mrb_value
mrb_big_divmod(mrb_state *mrb, mrb_value x, mrb_value y)
{
    mrb_value div, mod;
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y = mrb_int2big(mrb,y);
            break;
            
        case MRB_TT_BIGNUM:
            break;
            
        default:
            return mrb_funcall(mrb, x, "divmod", 1, y);
    }
    bigdivmod(mrb,x, y, &div, &mod);
    
    return mrb_assoc_new(mrb,bignorm(mrb,div), bignorm(mrb,mod));
}

mrb_value
n_mrb_big_divmod(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_divmod(mrb, x, y);
}

static int
bdigbitsize(mrb_state *mrb, BDIGIT x)
{
    int size = 1;
    int nb = BITSPERDIG / 2;
    BDIGIT bits = (~0 << nb);
    
    if (!x) return 0;
    while (x > 1) {
        if (x & bits) {
            size += nb;
            x >>= nb;
        }
        x &= ~bits;
        nb /= 2;
        bits >>= nb;
    }
    
    return size;
}

static mrb_value
big_shift(mrb_state *mrb, mrb_value x, long n)
{
    if (n < 0)
        return big_lshift(mrb,x, (unsigned long)-n);
    else if (n > 0)
        return big_rshift(mrb,x, (unsigned long)n);
    return x;
}

static mrb_value
big_fdiv(mrb_state *mrb, mrb_value x, mrb_value y)
{
#define DBL_BIGDIG ((DBL_MANT_DIG + BITSPERDIG) / BITSPERDIG)
    mrb_value z;
    long l, ex, ey;
    int i;
    
    bigtrunc(mrb,x);
    l = mrb_bignum_len(x) - 1;
    ex = l * BITSPERDIG;
    ex += bdigbitsize(mrb,mrb_bignum_digits(x)[l]);
    ex -= 2 * DBL_BIGDIG * BITSPERDIG;
    if (ex) x = big_shift(mrb,x, ex);
    
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            y = mrb_int2big(mrb,y);
        case MRB_TT_BIGNUM: {
            bigtrunc(mrb,y);
            l = mrb_bignum_len(y) - 1;
            ey = l * BITSPERDIG;
            ey += bdigbitsize(mrb,mrb_bignum_digits(y)[l]);
            ey -= DBL_BIGDIG * BITSPERDIG;
            if (ey) y = big_shift(mrb,y, ey);
        bignum:
            bigdivrem(mrb,x, y, &z, 0);
            l = ex - ey;
#if SIZEOF_LONG > SIZEOF_INT
            {
                /* Visual C++ can't be here */
                if (l > INT_MAX) return mrb_float_value(INFINITY);
                if (l < INT_MIN) return mrb_float_value(0.0);
            }
#endif
            return mrb_float_value(ldexp(big2dbl(mrb,z), (int)l));
        }
        case MRB_TT_FLOAT:
            y = dbl2big(mrb,ldexp(frexp(y.value.f, &i), DBL_MANT_DIG));
            ey = i - DBL_MANT_DIG;
            goto bignum;
            break;
        default:
            break;
    }
    mrb_raise(mrb,mrb->bignum_class,"big_fdiv");
    /* NOTREACHED */
    return mrb_nil_value();
}

/*
 *  call-seq:
 *     big.fdiv(numeric) -> float
 *
 *  Returns the floating point result of dividing <i>big</i> by
 *  <i>numeric</i>.
 *
 *     -1234567890987654321.fdiv(13731)      #=> -89910996357705.5
 *     -1234567890987654321.fdiv(13731.24)   #=> -89909424858035.7
 *
 */


mrb_value
mrb_big_fdiv(mrb_state *mrb, mrb_value x, mrb_value y)
{
    double dx, dy;
    
    dx = big2dbl(mrb,x);
    switch (mrb_type(y)) {
        case MRB_TT_FIXNUM:
            dy = y.value.f;
            if (isinf(dx))
                return big_fdiv(mrb,x, y);
            break;
            
        case MRB_TT_BIGNUM:
            dy = mrb_big2dbl(mrb,y);
            if (isinf(dx) || isinf(dy))
                return big_fdiv(mrb,x, y);
            break;
            
        case MRB_TT_FLOAT:
            dy = y.value.f;
            if (isnan(dy))
                return y;
            if (isinf(dx))
                return big_fdiv(mrb,x, y);
            break;
            
        default:
            return mrb_funcall(mrb,x,"fdiv",1,y);
    }
    return mrb_float_value(dx / dy);
}

mrb_value
n_mrb_big_fdiv(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_fdiv(mrb, x, y);
}





static mrb_value
bigsqr(mrb_state *mrb, mrb_value x)
{
    return bigtrunc(mrb,bigmul0(mrb,x, x));
}

/*
 *  call-seq:
 *     big ** exponent   -> numeric
 *
 *  Raises _big_ to the _exponent_ power (which may be an integer, float,
 *  or anything that will coerce to a number). The result may be
 *  a Fixnum, Bignum, or Float
 *
 *    123456789 ** 2      #=> 15241578750190521
 *    123456789 ** 1.2    #=> 5126464716.09932
 *    123456789 ** -2     #=> 6.5610001194102e-17
 */

mrb_value
mrb_big_pow(mrb_state *mrb, mrb_value x, mrb_value y)
{
    double d;
    SIGNED_VALUE yy;
    
    if (y.value.i == 0) return mrb_fixnum_value(1);
    switch (mrb_type(y)) {
        case MRB_TT_FLOAT:
            d = y.value.f;
            if ((!mrb_bignum_sign(x) && !BIGZEROP(x)) && d != round(d))
                return mrb_funcall(mrb,x, "**", 1, y);
            break;
            
        case MRB_TT_BIGNUM:
            mrb_raise(mrb,mrb->bignum_class,"in a**b, b may be too big");
            d = mrb_big2dbl(mrb,y);
            break;
            
        case MRB_TT_FIXNUM:
            yy = y.value.i;
            
            if (yy < 0)
                return mrb_funcall(mrb,x,"**", 1, y);
            else {
                mrb_value z = mrb_fixnum_value(0);
                SIGNED_VALUE mask;
                const long xlen = mrb_bignum_len(x) - 1;
                const long xbits = ffs(mrb_bignum_digits(x)[xlen]) + SIZEOF_BDIGITS*BITSPERDIG*xlen;
                const long BIGLEN_LIMIT = BITSPERDIG*1024*1024;
                
                if ((xbits > BIGLEN_LIMIT) || (xbits * yy > BIGLEN_LIMIT)) {
                    printf("in a**b, b may be too big\n");
                    d = (double)yy;
                    break;
                }
                for (mask = FIXNUM_MAX + 1; mask; mask >>= 1) {
                    if (z.value.i) z = bigsqr(mrb,z);
                    if (yy & mask) {
                        z = z.value.i ? bigtrunc(mrb,bigmul0(mrb,z, x)) : x;
                    }
                }
                return bignorm(mrb,z);
            }
            /* NOTREACHED */
            break;
            
        default:
            return mrb_funcall(mrb,x,"**", 1,y);
    }
    return mrb_fixnum_value(pow(mrb_big2dbl(mrb,x), d));
}

mrb_value
n_mrb_big_pow(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_pow(mrb, x, y);
}




static mrb_value
bigand_int(mrb_state *mrb, mrb_value x, long y)
{
    mrb_value z;
    BDIGIT *xds, *zds;
    long xn, zn;
    long i;
    char sign;
    
    if (y == 0) return mrb_fixnum_value(0);
    sign = (y > 0);
    xds = mrb_bignum_digits(x);
    zn = xn = mrb_bignum_len(x);
#if SIZEOF_mrb_bignum_digits == SIZEOF_LONG
    if (sign) {
        y &= xds[0];
        return LONG2NUM(y);
    }
#endif
    
    z = mrb_bignum_new(mrb,zn, mrb_bignum_sign(x) || sign);
    zds = mrb_bignum_digits(z);
    
#if SIZEOF_mrb_bignum_digits == SIZEOF_LONG
    i = 1;
    zds[0] = xds[0] & y;
#else
    {
        BDIGIT_DBL num = y;
        
        for (i=0; i<(int)(sizeof(y)/sizeof(BDIGIT)); i++) {
            zds[i] = xds[i] & BIGLO(num);
            num = BIGDN(num);
        }
    }
#endif
    while (i < xn) {
        zds[i] = sign?0:xds[i];
        i++;
    }
    if (!mrb_bignum_sign(z)) get2comp(mrb,z);
    return bignorm(mrb,z);
}

/*
 * call-seq:
 *     big & numeric   ->  integer
 *
 * Performs bitwise +and+ between _big_ and _numeric_.
 */

mrb_value
mrb_big_and(mrb_state *mrb, mrb_value xx, mrb_value yy)
{
    mrb_value x, y, z;
    BDIGIT *ds1, *ds2, *zds;
    long i, l1, l2;
    char sign;
    
    x = xx;
    y = bit_coerce(mrb,yy);
    if (!mrb_bignum_sign(x)) {
        x = mrb_big_clone(mrb,x);
        get2comp(mrb,x);
    }
    if (mrb_fixnum_p(y)) {
        return bigand_int(mrb,x, y.value.i);
    }
    if (!mrb_bignum_sign(y)) {
        y = mrb_big_clone(mrb,y);
        get2comp(mrb,y);
    }
    if (mrb_bignum_len(x) > mrb_bignum_len(y)) {
        l1 = mrb_bignum_len(y);
        l2 = mrb_bignum_len(x);
        ds1 = mrb_bignum_digits(y);
        ds2 = mrb_bignum_digits(x);
        sign = mrb_bignum_sign(y);
    }
    else {
        l1 = mrb_bignum_len(x);
        l2 = mrb_bignum_len(y);
        ds1 = mrb_bignum_digits(x);
        ds2 = mrb_bignum_digits(y);
        sign = mrb_bignum_sign(x);
    }
    z = mrb_bignum_new(mrb,l2, mrb_bignum_sign(x) || mrb_bignum_sign(y));
    zds = mrb_bignum_digits(z);
    
    for (i=0; i<l1; i++) {
        zds[i] = ds1[i] & ds2[i];
    }
    for (; i<l2; i++) {
        zds[i] = sign?0:ds2[i];
    }
    if (!mrb_bignum_sign(z)) get2comp(mrb,z);
    return bignorm(mrb,z);
}

mrb_value
n_mrb_big_and(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_and(mrb, x, y);
}




static mrb_value
bigor_int(mrb_state *mrb, mrb_value x, long y)
{
    mrb_value z;
    BDIGIT *xds, *zds;
    long xn, zn;
    long i;
    char sign;
    
    sign = (y >= 0);
    xds = mrb_bignum_digits(x);
    zn = xn = mrb_bignum_len(x);
    z = mrb_bignum_new(mrb,zn, mrb_bignum_sign(x) && sign);
    zds = mrb_bignum_digits(z);
    
#if SIZEOF_mrb_bignum_digits == SIZEOF_LONG
    i = 1;
    zds[0] = xds[0] | y;
#else
    {
        BDIGIT_DBL num = y;
        
        for (i=0; i<(int)(sizeof(y)/sizeof(BDIGIT)); i++) {
            zds[i] = xds[i] | BIGLO(num);
            num = BIGDN(num);
        }
    }
#endif
    while (i < xn) {
        zds[i] = sign?xds[i]:(BDIGIT)(BIGRAD-1);
        i++;
    }
    if (!mrb_bignum_sign(z)) get2comp(mrb,z);
    return bignorm(mrb,z);
}

/*
 * call-seq:
 *     big | numeric   ->  integer
 *
 * Performs bitwise +or+ between _big_ and _numeric_.
 */

mrb_value
mrb_big_or(mrb_state *mrb, mrb_value xx, mrb_value yy)
{
    volatile mrb_value x, y, z;
    BDIGIT *ds1, *ds2, *zds;
    long i, l1, l2;
    char sign;
    
    x = xx;
    y = bit_coerce(mrb,yy);
    
    if (!mrb_bignum_sign(x)) {
        x = mrb_big_clone(mrb,x);
        get2comp(mrb,x);
    }
    if (mrb_fixnum_p(y)) {
        return bigor_int(mrb,x, y.value.i);
    }
    if (!mrb_bignum_sign(y)) {
        y = mrb_big_clone(mrb,y);
        get2comp(mrb,y);
    }
    if (mrb_bignum_len(x) > mrb_bignum_len(y)) {
        l1 = mrb_bignum_len(y);
        l2 = mrb_bignum_len(x);
        ds1 = mrb_bignum_digits(y);
        ds2 = mrb_bignum_digits(x);
        sign = mrb_bignum_sign(y);
    }
    else {
        l1 = mrb_bignum_len(x);
        l2 = mrb_bignum_len(y);
        ds1 = mrb_bignum_digits(x);
        ds2 = mrb_bignum_digits(y);
        sign = mrb_bignum_sign(x);
    }
    z = mrb_bignum_new(mrb,l2, mrb_bignum_sign(x) && mrb_bignum_sign(y));
    zds = mrb_bignum_digits(z);
    
    for (i=0; i<l1; i++) {
        zds[i] = ds1[i] | ds2[i];
    }
    for (; i<l2; i++) {
        zds[i] = sign?ds2[i]:(BDIGIT)(BIGRAD-1);
    }
    if (!mrb_bignum_sign(z)) get2comp(mrb,z);
    return bignorm(mrb,z);
}

mrb_value
n_mrb_big_or(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_or(mrb, x, y);
}




static mrb_value
bigxor_int(mrb_state *mrb, mrb_value x, long y)
{
    mrb_value z;
    BDIGIT *xds, *zds;
    long xn, zn;
    long i;
    char sign;
    
    sign = (y >= 0) ? 1 : 0;
    xds = mrb_bignum_digits(x);
    zn = xn = mrb_bignum_len(x);
    z = mrb_bignum_new(mrb,zn, !(mrb_bignum_sign(x) ^ sign));
    zds = mrb_bignum_digits(z);
    
#if SIZEOF_mrb_bignum_digits == SIZEOF_LONG
    i = 1;
    zds[0] = xds[0] ^ y;
#else
    {
        BDIGIT_DBL num = y;
        
        for (i=0; i<(int)(sizeof(y)/sizeof(BDIGIT)); i++) {
            zds[i] = xds[i] ^ BIGLO(num);
            num = BIGDN(num);
        }
    }
#endif
    while (i < xn) {
        zds[i] = sign?xds[i]:~xds[i];
        i++;
    }
    if (!mrb_bignum_sign(z)) get2comp(mrb,z);
    return bignorm(mrb,z);
}
/*
 * call-seq:
 *     big ^ numeric   ->  integer
 *
 * Performs bitwise +exclusive or+ between _big_ and _numeric_.
 */

mrb_value
mrb_big_xor(mrb_state *mrb, mrb_value xx, mrb_value yy)
{
    volatile mrb_value x, y;
    mrb_value z;
    BDIGIT *ds1, *ds2, *zds;
    long i, l1, l2;
    char sign;
    
    x = xx;
    y = bit_coerce(mrb,yy);
    
    if (!mrb_bignum_sign(x)) {
        x = mrb_big_clone(mrb,x);
        get2comp(mrb,x);
    }
    if (mrb_fixnum_p(y)) {
        return bigxor_int(mrb,x,y.value.i);
    }
    if (!mrb_bignum_sign(y)) {
        y = mrb_big_clone(mrb,y);
        get2comp(mrb,y);
    }
    if (mrb_bignum_len(x) > mrb_bignum_len(y)) {
        l1 = mrb_bignum_len(y);
        l2 = mrb_bignum_len(x);
        ds1 = mrb_bignum_digits(y);
        ds2 = mrb_bignum_digits(x);
        sign = mrb_bignum_sign(y);
    }
    else {
        l1 = mrb_bignum_len(x);
        l2 = mrb_bignum_len(y);
        ds1 = mrb_bignum_digits(x);
        ds2 = mrb_bignum_digits(y);
        sign = mrb_bignum_sign(x);
    }
    mrb_bignum_set_sign(x, mrb_bignum_sign(x)?1:0);
    mrb_bignum_set_sign(y, mrb_bignum_sign(y)?1:0);
    z = mrb_bignum_new(mrb,l2, !(mrb_bignum_sign(x) ^ mrb_bignum_sign(y)));
    zds = mrb_bignum_digits(z);
    
    for (i=0; i<l1; i++) {
        zds[i] = ds1[i] ^ ds2[i];
    }
    for (; i<l2; i++) {
        zds[i] = sign?ds2[i]:~ds2[i];
    }
    if (!mrb_bignum_sign(z)) get2comp(mrb,z);
    
    return bignorm(mrb,z);
}


mrb_value
n_mrb_big_xor(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_xor(mrb, x, y);
}



static mrb_value
check_shiftdown(mrb_state *mrb, mrb_value y, mrb_value x)
{
    if (!mrb_bignum_len(x)) return mrb_fixnum_value(0);
    if (mrb_bignum_len(y) > SIZEOF_LONG / SIZEOF_BDIGITS) {
        return mrb_bignum_sign(x) ? mrb_fixnum_value(0) : mrb_fixnum_value(-1);
    }
    return mrb_fixnum_value(MRB_TT_NIL);
}

/*
 * call-seq:
 *     big << numeric   ->  integer
 *
 * Shifts big left _numeric_ positions (right if _numeric_ is negative).
 */

mrb_value
mrb_big_lshift(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long shift;
    int neg = 0;
    
    for (;;) {
        if (mrb_fixnum_p(y)) {
            shift = y.value.i;
            if (shift < 0) {
                neg = 1;
                shift = -shift;
            }
            break;
        }
        else if (mrb_type(y) == MRB_TT_BIGNUM) {
            if (!mrb_bignum_sign(y)) {
                mrb_value t = check_shiftdown(mrb,y, x);
                if (!mrb_nil_p(t)) return t;
                neg = 1;
            }
            shift = big2ulong(mrb, y, "long", TRUE).value.i;
            break;
        }
        y = mrb_to_int(mrb,y);
    }
    
    x = neg ? big_rshift(mrb,x, shift) : big_lshift(mrb,x, shift);
    return bignorm(mrb,x);
}

mrb_value
n_mrb_big_lshift(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_lshift(mrb, x, y);
}




static mrb_value
big_lshift(mrb_state *mrb, mrb_value x, unsigned long shift)
{
    BDIGIT *xds, *zds;
    long s1 = shift/BITSPERDIG;
    int s2 = (int)(shift%BITSPERDIG);
    mrb_value z;
    BDIGIT_DBL num = 0;
    long len, i;
    
    len = mrb_bignum_len(x);
    z = mrb_bignum_new(mrb,len+s1+1, mrb_bignum_sign(x));
    zds = mrb_bignum_digits(z);
    for (i=0; i<s1; i++) {
        *zds++ = 0;
    }
    xds = mrb_bignum_digits(x);
    for (i=0; i<len; i++) {
        num = num | (BDIGIT_DBL)*xds++<<s2;
        *zds++ = BIGLO(num);
        num = BIGDN(num);
    }
    *zds = BIGLO(num);
    return z;
}

/*
 * call-seq:
 *     big >> numeric   ->  integer
 *
 * Shifts big right _numeric_ positions (left if _numeric_ is negative).
 */

mrb_value
mrb_big_rshift(mrb_state *mrb, mrb_value x, mrb_value y)
{
    long shift;
    int neg = 0;
    
    for (;;) {
        if (mrb_fixnum_p(y)) {
            shift = y.value.i;
            if (shift < 0) {
                neg = 1;
                shift = -shift;
            }
            break;
        }
        else if (mrb_type(y) == MRB_TT_BIGNUM) {
            if (mrb_bignum_sign(y)) {
                mrb_value t = check_shiftdown(mrb,y, x);
                if (!mrb_nil_p(t)) return t;
            }
            else {
                neg = 1;
            }
            shift = big2ulong(mrb, y, "long", TRUE).value.i;
            break;
        }
        y = mrb_to_int(mrb,y);
    }
    
    x = neg ? big_lshift(mrb,x, shift) : big_rshift(mrb,x, shift);
    return bignorm(mrb,x);
}

mrb_value
n_mrb_big_rshift(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_rshift(mrb, x, y);
}



static mrb_value
big_rshift(mrb_state *mrb, mrb_value x, unsigned long shift)
{
    BDIGIT *xds, *zds;
    long s1 = shift/BITSPERDIG;
    int s2 = (int)(shift%BITSPERDIG);
    mrb_value z;
    BDIGIT_DBL num = 0;
    long i, j;
    volatile mrb_value save_x;
    
    if (s1 > mrb_bignum_len(x)) {
        if (mrb_bignum_sign(x))
            return mrb_fixnum_value(0);
        else
            return mrb_fixnum_value(-1);
    }
    if (!mrb_bignum_sign(x)) {
        save_x = x = mrb_big_clone(mrb,x);
        get2comp(mrb,x);
    }
    xds = mrb_bignum_digits(x);
    i = mrb_bignum_len(x); j = i - s1;
    if (j == 0) {
        if (mrb_bignum_sign(x)) return mrb_fixnum_value(0);
        else return mrb_fixnum_value(-1);
    }
    z = mrb_bignum_new(mrb,j, mrb_bignum_sign(x));
    if (!mrb_bignum_sign(x)) {
        num = ((BDIGIT_DBL)~0) << BITSPERDIG;
    }
    zds = mrb_bignum_digits(z);
    while (i--, j--) {
        num = (num | xds[i]) >> s2;
        zds[j] = BIGLO(num);
        num = BIGUP(xds[i]);
    }
    if (!mrb_bignum_sign(x)) {
        get2comp(mrb,z);
    }
    return z;
}

/*
 *  call-seq:
 *     big[n] -> 0, 1
 *
 *  Bit Reference---Returns the <em>n</em>th bit in the (assumed) binary
 *  representation of <i>big</i>, where <i>big</i>[0] is the least
 *  significant bit.
 *
 *     a = 9**15
 *     50.downto(0) do |n|
 *       print a[n]
 *     end
 *
 *  <em>produces:</em>
 *
 *     000101110110100000111000011110010100111100010111001
 *
 */

static mrb_value
mrb_big_aref(mrb_state *mrb, mrb_value x, mrb_value y)
{
    BDIGIT *xds;
    BDIGIT_DBL num;
    mrb_value shift;
    long i, s1, s2;
    
    if (mrb_type(y) == MRB_TT_BIGNUM) {
        if (!mrb_bignum_sign(y))
            return mrb_fixnum_value(0);
        bigtrunc(mrb,y);
        if (mrb_bignum_len(y) > DIGSPERLONG) {
        out_of_range:
            return mrb_bignum_sign(x) ? mrb_fixnum_value(0) : mrb_fixnum_value(1);
        }
        shift = big2ulong(mrb,y, "long", FALSE);
    }
    else {
        i = y.value.i;
        if (i < 0) return mrb_fixnum_value(0);
        shift = mrb_fixnum_value(i);
    }
    s1 = shift.value.i/BITSPERDIG;
    s2 = shift.value.i%BITSPERDIG;
    
    if (s1 >= mrb_bignum_len(x)) goto out_of_range;
    if (!mrb_bignum_sign(x)) {
        xds = mrb_bignum_digits(x);
        i = 0; num = 1;
        while (num += ~xds[i], ++i <= s1) {
            num = BIGDN(num);
        }
    }
    else {
        num = mrb_bignum_digits(x)[s1];
    }
    if (num & ((BDIGIT_DBL)1<<s2))
        return mrb_fixnum_value(1);
    return mrb_fixnum_value(0);
}

mrb_value
n_mrb_big_aref(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_aref(mrb, x, y);
}



/*
 * call-seq:
 *   big.hash   -> fixnum
 *
 * Compute a hash based on the mrb_value of _big_.
 */

static mrb_value
mrb_big_hash(mrb_state *mrb, mrb_value x)
{
    //st_index_t hash;
    
    //hash = rb_memhash(mrb_bignum_digits(x), sizeof(BDIGIT)*mrb_bignum_len(x)) ^ mrb_bignum_sign(x);
    return mrb_fixnum_value(mrb_obj_id(x));
}




/*
 * MISSING: documentation
 */

static mrb_value
mrb_big_coerce(mrb_state *mrb, mrb_value x, mrb_value y)
{
    if (mrb_fixnum_p(y)) {
        return mrb_assoc_new(mrb,mrb_int2big(mrb,y), x);
    }
    else if (mrb_type(y) == MRB_TT_BIGNUM) {
        return mrb_assoc_new(mrb,y, x);
    }
    else {
        mrb_raisef(mrb,mrb->bignum_class, "can't coerce %s to Bignum",
                 mrb_obj_classname(mrb,y));
    }
    /* not reached */
    return mrb_nil_value();
}


mrb_value
n_mrb_big_coerce(mrb_state *mrb, mrb_value x)
{
    mrb_value y;
    
    mrb_get_args(mrb, "o", &y);
    
    return mrb_big_coerce(mrb, x, y);
}



/*
 *  call-seq:
 *     big.abs -> aBignum
 *
 *  Returns the absolute mrb_value of <i>big</i>.
 *
 *     -1234567890987654321.abs   #=> 1234567890987654321
 */

static mrb_value
mrb_big_abs(mrb_state *mrb, mrb_value x)
{
    if (!mrb_bignum_sign(x)) {
        x = mrb_big_clone(mrb,x);
        mrb_bignum_set_sign(x, 1);
    }
    return x;
}

/*
 *  call-seq:
 *     big.size -> integer
 *
 *  Returns the number of bytes in the machine representation of
 *  <i>big</i>.
 *
 *     (256**10 - 1).size   #=> 12
 *     (256**20 - 1).size   #=> 20
 *     (256**40 - 1).size   #=> 40
 */

static mrb_value
mrb_big_size(mrb_state *mrb,mrb_value big)
{
    return mrb_fixnum_value(mrb_bignum_len(big)*SIZEOF_BDIGITS);
}

/*
 *  call-seq:
 *     big.odd? -> true or false
 *
 *  Returns <code>true</code> if <i>big</i> is an odd number.
 */

static mrb_value
mrb_big_odd_p(mrb_state *mrb, mrb_value num)
{
    if (mrb_bignum_digits(num)[0] & 1) {
        return mrb_fixnum_value(MRB_TT_TRUE);
    }
    return mrb_fixnum_value(MRB_TT_FALSE);
}

/*
 *  call-seq:
 *     big.even? -> true or false
 *
 *  Returns <code>true</code> if <i>big</i> is an even number.
 */

static mrb_value
mrb_big_even_p(mrb_state *mrb, mrb_value num)
{
    if (mrb_bignum_digits(num)[0] & 1) {
        return mrb_fixnum_value(MRB_TT_FALSE);
    }
    return mrb_fixnum_value(MRB_TT_TRUE);
}

/*
 *  Bignum objects hold integers outside the range of
 *  Fixnum. Bignum objects are created
 *  automatically when integer calculations would otherwise overflow a
 *  Fixnum. When a calculation involving
 *  Bignum objects returns a result that will fit in a
 *  Fixnum, the result is automatically converted.
 *
 *  For the purposes of the bitwise operations and <code>[]</code>, a
 *  Bignum is treated as if it were an infinite-length
 *  bitstring with 2's complement representation.
 *
 *  While Fixnum mrb_values are immediate, Bignum
 *  objects are not---assignment and parameter passing work with
 *  references to objects, not the objects themselves.
 *
 */

/*
   
 */

void
mrb_bignum_gem_init(mrb_state* mrb) 
{
    mrb->bignum_class = mrb_define_class(mrb, "Bignum", mrb->object_class);
    
    mrb_define_method(mrb,mrb->bignum_class, "to_s", n_mrb_big_to_s, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "coerce", n_mrb_big_coerce, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "-@", mrb_big_uminus, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "+", n_mrb_big_plus, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "-", n_mrb_big_minus, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "*", n_mrb_big_mul, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "/", n_mrb_big_div, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "%", n_mrb_big_modulo, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "div", n_mrb_big_div, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "divmod", n_mrb_big_divmod, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "modulo", n_mrb_big_modulo, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "remainder", n_mrb_big_remainder, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "fdiv", n_mrb_big_fdiv, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "**", n_mrb_big_pow, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "&", n_mrb_big_and, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "|", n_mrb_big_or, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "^", n_mrb_big_xor, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "~", mrb_big_neg, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "<<", n_mrb_big_lshift, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, ">>", n_mrb_big_rshift, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "[]", n_mrb_big_aref, ARGS_REQ(1));
    
    mrb_define_method(mrb,mrb->bignum_class, "<=>", n_mrb_big_cmp, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "==", n_mrb_big_eq, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, ">", n_mrb_big_gt, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, ">=", n_mrb_big_ge, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "<", n_mrb_big_lt, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "<=", n_mrb_big_le, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "===", n_mrb_big_eq, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "eql?", n_mrb_big_eql, ARGS_REQ(1));
    mrb_define_method(mrb,mrb->bignum_class, "hash", mrb_big_hash, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "to_f", mrb_big_to_f, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "abs", mrb_big_abs, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "magnitude", mrb_big_abs, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "size", mrb_big_size, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "odd?", mrb_big_odd_p, ARGS_NONE());
    mrb_define_method(mrb,mrb->bignum_class, "even?", mrb_big_even_p, ARGS_NONE());
    
    power_cache_init(mrb);
    big_three = mrb_uint2big(mrb,mrb_fixnum_value(3));
    mrb_gc_mark_value(mrb, big_three);
}

