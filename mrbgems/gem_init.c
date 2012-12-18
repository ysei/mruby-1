/*
 * This file contains a list of all
 * initializing methods which are
 * necessary to bootstrap all gems.
 *
 * IMPORTANT:
 *   This file was generated!
 *   All manual changes will get lost.
 */

#include "mruby.h"

void GENERATED_TMP_mrb_bignum_gem_init(mrb_state*);void GENERATED_TMP_mrb_require_gem_init(mrb_state*);void GENERATED_TMP_mrb_c_and_ruby_extension_example_gem_init(mrb_state*);

void
mrb_init_mrbgems(mrb_state *mrb) {
  GENERATED_TMP_mrb_bignum_gem_init(mrb);  GENERATED_TMP_mrb_require_gem_init(mrb);  GENERATED_TMP_mrb_c_and_ruby_extension_example_gem_init(mrb);
}
