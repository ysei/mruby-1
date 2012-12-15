//
//  safe.h
//  mruby
//
//  Created by Christian Mauceri on 12/12/12.
//
//

#ifndef mruby_safe_h
#define mruby_safe_h
#include "mruby.h"


void rb_secure(int);
int rb_safe_level(void);
void rb_set_safe_level(int);
void rb_set_safe_level_force(int);
void rb_secure_update(mrb_value);
void rb_insecure_operation(void);



#endif
