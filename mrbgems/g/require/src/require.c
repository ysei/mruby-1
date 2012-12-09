#include "mruby.h"
#include "mruby/string.h"

#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/variable.h"
#include "mruby/compile.h"
#include "mruby/hash.h"
#include "mruby/irep.h"
#include "mruby/proc.h"
#include "mruby/compile.h"

#include "opcode.h"

#include <stdio.h>
mrb_value mrb_yield_internal(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv, mrb_value self, struct RClass *c);

mrb_value eval_string(mrb_state *mrb, mrb_value mod, char* str)
{
    struct mrb_parser_state *parser;
    mrbc_context *cxt;
    int n;
    mrb_value result;
    int arena_idx;
    
    arena_idx = mrb_gc_arena_save(mrb);
    cxt = mrbc_context_new(mrb);
    parser = mrb_parser_new(mrb);
    parser->s = str;
    parser->send = str + strlen(str);
    parser->lineno = 1;
    mrb_parser_parse(parser, cxt);
    
    if (0 < parser->nerr) {
        /* syntax error */
        mrb_raise(mrb, E_SYNTAX_ERROR, str);
    } else {
        /* generate bytecode */
        n = mrb_generate_code(mrb, parser);
        
        /* evaluate the bytecode */
        result = mrb_run(mrb, mrb_proc_new(mrb, mrb->irep[n]), mrb_top_self(mrb));
        //mrb_top_self(mrb));
        mrb_parser_free(parser);
        mrbc_context_free(mrb, cxt);
        mrb_gc_arena_restore(mrb, arena_idx);
        
        if (FIXNUM_P(result)) {
            mrb_run_irep_as_proc(mrb, mrb->irep[mrb_fixnum(result)]);
        }
        else if (mrb->exc) {
            // fail to load.
            longjmp(*(jmp_buf*)mrb->jmp, 1);
        }
    }
    mrb_raise(mrb, E_NOTIMP_ERROR, "module_eval/class_eval with string not implemented");
    return mrb_nil_value();
}
/* 15.2.2.4.35 */
/*
 *  call-seq:
 *     mod.class_eval {| | block }  -> obj
 *     mod.module_eval {| | block } -> obj
 *
 *  Evaluates block in the context of _mod_. This can
 *  be used to add methods to a class. <code>module_eval</code> returns
 *  the result of evaluating its argument.
 */

mrb_value
mrb_mod_module_eval(mrb_state *mrb, mrb_value mod)
{
    mrb_value a, b;
    struct RClass *c;
    mrb_value result;
    
    if (mrb_get_args(mrb, "|S&", &a, &b) == 1) {
        char *str = mrb_string_value_ptr(mrb,a);
        fprintf(stderr,"%s\n",str);
        
        result = eval_string(mrb, mod, str);
        return result;
    }
    c = mrb_class_ptr(mod);
    return mrb_yield_internal(mrb, b, 0, 0, mod, c);
}

static void
load_file(mrb_state *mrb, mrb_value filename)
{
    FILE *fp;
    mrb_value str, ret;
    mrbc_context *c;
    int arena_idx;
    
    arena_idx = mrb_gc_arena_save(mrb);
    str = mrb_str_new(mrb, RSTRING_PTR(filename), RSTRING_LEN(filename));
    c = mrbc_context_new(mrb);
    c->capture_errors = 1;
    c->no_exec = 1;
    mrbc_filename(mrb, c, RSTRING_PTR(str));
    
    fp = fopen(c->filename, "r");
    if (!fp) {
        mrbc_context_free(mrb, c);
        mrb_raisef(mrb, E_LOAD_ERROR, "file '%s' not found.", RSTRING_PTR(str));
    }
    ret = mrb_load_file_cxt(mrb, fp, c);
    fclose(fp);
    mrbc_context_free(mrb, c);
    mrb_gc_arena_restore(mrb, arena_idx);
    
    if (FIXNUM_P(ret)) {
        mrb_run_irep_as_proc(mrb, mrb->irep[mrb_fixnum(ret)]);
    }
    else if (mrb->exc) {
        // fail to load.
        longjmp(*(jmp_buf*)mrb->jmp, 1);
    }
}



mrb_value
mrb_f_load(mrb_state *mrb, mrb_value self)
{
    mrb_value filename;
    
    mrb_get_args(mrb, "S", &filename);
    load_file(mrb, filename);
    return mrb_true_value();
}

mrb_value
mrb_f_require(mrb_state *mrb, mrb_value self)
{
    mrb_value filename, loaded_features, loading_features;
    int i, len;
    mrb_sym sym_loaded_features, sym_loading_features;
    jmp_buf c_jmp;
    jmp_buf *prev_jmp;
    mrb_value gv,path;
    
    mrb_get_args(mrb, "S", &filename);
    /* If filename should be expanded to absolute path, modify it here. */
    mrb_value argv[1];
    
    
    gv = mrb_class_new_instance(mrb, 1, argv, mrb_class_get(mrb, "GlobalVars"));
    
    argv[0] = mrb_str_new2(mrb, "path");
    path = mrb_funcall_argv(mrb, gv, mrb_intern(mrb, "get"), 1,argv);
    
    filename = mrb_str_append(mrb, path, filename);
    
    /* Check LOADED_FEATURES */
    sym_loaded_features = mrb_intern(mrb, "$LOADED_FEATURES");
    loaded_features = mrb_gv_get(mrb, sym_loaded_features);
    if (mrb_nil_p(loaded_features)) {
        loaded_features = mrb_ary_new(mrb);
        mrb_gv_set(mrb, sym_loaded_features, loaded_features);
    }
    len = RARRAY_LEN(loaded_features);
    
    for (i=0; i<len; i++) {
        if (mrb_str_cmp(mrb, RARRAY_PTR(loaded_features)[i], filename) == 0) break;
    }
    if (i != len) return mrb_false_value();
    
    /* Check __loading_features__ */
    sym_loading_features = mrb_intern(mrb, "__loading_features__");
    loading_features = mrb_gv_get(mrb, sym_loading_features);
    if (mrb_nil_p(loading_features)) {
        loading_features = mrb_hash_new(mrb);
        mrb_gv_set(mrb, sym_loading_features, loading_features);
    }
    if (!mrb_nil_p(mrb_hash_fetch(mrb, loading_features, filename, mrb_nil_value())))
        return mrb_false_value();
    mrb_hash_set(mrb, loading_features, filename, mrb_true_value());
    
    prev_jmp = mrb->jmp;
    if (setjmp(c_jmp) != 0) {
        mrb->jmp = prev_jmp;
        mrb_hash_delete_key(mrb, loading_features, filename);
        longjmp(*(jmp_buf*)mrb->jmp, 1);
    }
    mrb->jmp = &c_jmp;
    
    load_file(mrb, filename);
    mrb->jmp = prev_jmp;
    
    mrb_hash_delete_key(mrb, loading_features, filename);
    mrb_ary_push(mrb, loaded_features, filename);
    
    return mrb_true_value();
}

void
mrb_require_gem_init(mrb_state *mrb)
{
    struct RClass *k = mrb->kernel_module;
    struct RClass *m = mrb->module_class;
   fprintf(stderr,"mrb_init_require\n");
    
    mrb_define_method(mrb, k, "require",                    mrb_f_require,                   ARGS_REQ(1));
    mrb_define_method(mrb, k, "load",                       mrb_f_load,                      ARGS_REQ(1));
    mrb_define_method(mrb, m, "module_eval",                mrb_mod_module_eval, ARGS_ANY());            /* 15.2.2.4.35 */
    mrb_define_method(mrb, m, "class_eval",                mrb_mod_module_eval, ARGS_ANY());            /* 15.2.2.4.35 */
}
