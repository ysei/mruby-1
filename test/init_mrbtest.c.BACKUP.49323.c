#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/dump.h"
#include "mruby/string.h"
#include "mruby/proc.h"

extern const char mrbtest_irep[];
extern const char mrbgemtest_irep[];

void
mrb_init_mrbtest(mrb_state *mrb)
{
<<<<<<< HEAD
  int n = mrb_read_irep(mrb, mrbtest_irep);
#ifdef ENABLE_GEMS
  int m = mrb_read_irep(mrb, mrbgemtest_irep);
#endif

  mrb_run(mrb, mrb_proc_new(mrb, mrb->irep[n]), mrb_top_self(mrb));
#ifdef ENABLE_GEMS
  mrb_run(mrb, mrb_proc_new(mrb, mrb->irep[m]), mrb_top_self(mrb));
#endif
=======
  mrb_load_irep(mrb, mrbtest_irep);
>>>>>>> b8e5a570479a2e45d8746d85df15f84b619466d6
  if (mrb->exc) {
    mrb_p(mrb, mrb_obj_value(mrb->exc));
    exit(0);
  }
}

