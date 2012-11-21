/*
 ** enum.c - GlobalVar module
 **
 ** See Copyright Notice in mruby.h
 */

#include "mruby.h"
#include "opcode.h"
#include "mruby/irep.h"
#include "mruby/variable.h"
#include "mruby/proc.h"
#include "mruby/array.h"
#include "mruby/string.h"
#include "mruby/hash.h"
#include "mruby/range.h"
#include "mruby/class.h"
#include "mruby/numeric.h"
#include "mruby/compile.h"

#include "error.h"
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdarg.h>



void
mrb_init_gvar(mrb_state *mrb)
{
   mrb_define_module(mrb, "GlobalVar");   
}

