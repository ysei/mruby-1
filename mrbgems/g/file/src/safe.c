//
//  safe.c
//  mruby
//
//  Created by Christian Mauceri on 12/12/12.
//
//

#include <stdio.h>
#include "safe.h"
#define SAFE_LEVEL_MAX 4

/* $SAFE accessor */

int
mrb_safe_level(void)
{
    //return GET_THREAD()->safe_level;
    return SAFE_LEVEL_MAX;
}

void
rb_set_safe_level_force(int safe)
{
    //GET_THREAD()->safe_level = safe;
}

void
rb_set_safe_level(int level)
{
}

static mrb_value
safe_getter(void)
{
    return (mrb_value)mrb_safe_level();
}

static void
safe_setter(VALUE val)
{
    int level = NUM2INT(val);
    rb_thread_t *th = GET_THREAD();
    
    if (level < th->safe_level) {
        rb_raise(rb_eSecurityError,
                 "tried to downgrade safe level from %d to %d",
                 th->safe_level, level);
    }
    if (level == 3) {
        rb_warning("$SAFE=3 does no sandboxing; you might want to use $SAFE=4");
    }
    if (level > SAFE_LEVEL_MAX) {
        level = SAFE_LEVEL_MAX;
    }
    th->safe_level = level;
}

void
rb_secure(int level)
{
    if (level <= rb_safe_level()) {
        if (rb_frame_callee()) {
            rb_raise(rb_eSecurityError, "Insecure operation `%s' at level %d",
                     rb_id2name(rb_frame_callee()), rb_safe_level());
        }
        else {
            rb_raise(rb_eSecurityError, "Insecure operation at level %d",
                     rb_safe_level());
        }
    }
}

void
rb_secure_update(VALUE obj)
{
    if (!OBJ_TAINTED(obj))
        rb_secure(4);
}

void
rb_insecure_operation(void)
{
    if (rb_frame_callee()) {
        rb_raise(rb_eSecurityError, "Insecure operation - %s",
                 rb_id2name(rb_frame_callee()));
    }
    else {
        rb_raise(rb_eSecurityError, "Insecure operation: -r");
    }
}

void
rb_check_safe_obj(VALUE x)
{
    if (rb_safe_level() > 0 && OBJ_TAINTED(x)) {
        rb_insecure_operation();
    }
    rb_secure(4);
}

void
rb_check_safe_str(VALUE x)
{
    rb_check_safe_obj(x);
    if (TYPE(x) != T_STRING) {
        rb_raise(rb_eTypeError, "wrong argument type %s (expected String)",
                 rb_obj_classname(x));
    }
}

void
Init_safe(void)
{
    rb_define_virtual_variable("$SAFE", safe_getter, safe_setter);
}
