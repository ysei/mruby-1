/*
** re.h - Regexp class
**
** See Copyright Notice in mruby.h
*/

#ifndef RE_H
#define RE_H

//#include <sys/types.h>
#include <stdio.h>

#include "regex.h"
#include "encoding.h"
#include "st.h"

#define MRUBY_OBJECT_HEADER \
enum mrb_vtype tt; \
unsigned int color:3;\
unsigned int flags:21;\
struct RClass *c;\
struct RBasic *gcnext

#define RBASIC(obj)  ((struct RBasic*)((obj).value.p))
#define RBASIC_KLASS(v) ((struct RClass *)(((struct RBasic*)((v).value.p))->c))
#define ROBJECT(v) ((struct RObject*)((v).value.p))
#define ROBJECT_IVPTR(v) (((struct RObject*)((v).value.p))->iv)
#define ROBJECT_NUMIV(v) (ROBJECT_IVPTR(v) ? ROBJECT_IVPTR(v)->size : 0)

#define BEG(no) regs->beg[no]
#define END(no) regs->end[no]

struct rmatch_offset {
    long beg;
    long end;
};

struct rmatch {
    struct re_registers regs;

    int char_offset_updated;
    int char_offset_num_allocated;
    struct rmatch_offset *char_offset;
};

//struct RMatch {
//  MRUBY_OBJECT_HEADER;
//  mrb_value str;
//  struct re_registers *regs;
//};
struct RMatch {
  MRUBY_OBJECT_HEADER;
  struct RString *str;
  struct rmatch *rmatch;
  struct RRegexp *regexp;
};

struct RRegexp {
  MRUBY_OBJECT_HEADER;
  struct re_pattern_buffer *ptr;
  struct RString *src;
  unsigned long usecnt;
};

#define mrb_regex_ptr(r)    ((struct RRegexp*)((r).value.p))
#define RREGEXP(r)          ((struct RRegexp*)((r).value.p))
#define RREGEXP_SRC(r)      (RREGEXP(r)->src)
#define RREGEXP_SRC_PTR(r)  (RREGEXP_SRC(r)->ptr)
#define RREGEXP_SRC_LEN(r)  (RREGEXP_SRC(r)->len)
int re_adjust_startpos(struct re_pattern_buffer *bufp, const char *string, int size, int startpos, int range);

#define RE_OPTION_IGNORECASE ONIG_OPTION_IGNORECASE
#define RE_OPTION_EXTENDED   ONIG_OPTION_EXTEND
#define RE_OPTION_MULTILINE  ONIG_OPTION_MULTILINE

typedef struct re_pattern_buffer Regexp;

//#define RMATCH(obj)  (R_CAST(RMatch)(obj))
#define RMATCH_REGS(v)      (&((struct RMatch*)((v).value.p))->rmatch->regs)
#define RMATCH(v)           ((struct RMatch*)((v).value.p))
#define mrb_match_ptr(v)    ((struct RMatch*)((v).value.p))

int mrb_memcmp(const void *p1, const void *p2, int len);

mrb_int mrb_reg_search (mrb_state *mrb, mrb_value, mrb_value, mrb_int, mrb_int);
mrb_value mrb_reg_regsub (mrb_state *mrb, mrb_value, mrb_value, struct re_registers *, mrb_value);
//mrb_value mrb_reg_regsub(mrb_value, mrb_value, struct re_registers *, mrb_value);
mrb_int mrb_reg_adjust_startpos(mrb_state *mrb, mrb_value re, mrb_value str, mrb_int pos, mrb_int reverse);
void mrb_match_busy (mrb_value);

mrb_value mrb_reg_str_to_reg(mrb_state *mrb, mrb_value str);
mrb_value mrb_reg_quote(mrb_state *mrb, mrb_value str);
mrb_value mrb_reg_regcomp(mrb_state *mrb, mrb_value str);
mrb_value mrb_reg_new_str(mrb_state *mrb, mrb_value s, int options);
mrb_value mrb_reg_match_str(mrb_state *mrb, mrb_value re, mrb_value str);
mrb_value mrb_reg_nth_match(mrb_state *mrb, mrb_int nth, mrb_value match);
mrb_value mrb_backref_get(mrb_state *mrb);
//mrb_int mrb_memsearch(const void *x0, mrb_int m, const void *y0, mrb_int n);
mrb_value mrb_reg_to_s(mrb_state *mrb, mrb_value re);
void mrb_backref_set(mrb_state *mrb, mrb_value val);
mrb_value match_alloc(mrb_state *mrb);
int mrb_reg_backref_number(mrb_state *mrb, mrb_value match, mrb_value backref);

#endif
