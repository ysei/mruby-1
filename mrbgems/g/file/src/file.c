#include "mruby.h"
/**********************************************************************
 
 file.c -
 
 $Author: usa $
 created at: Mon Nov 15 12:24:34 JST 1993
 
 Copyright (C) 1993-2007 Yukihiro Matsumoto
 Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 Copyright (C) 2000  Information-technology Promotion Agency, Japan
 
 **********************************************************************/

#ifdef _WIN32
#include "missing/file.h"
#endif
#ifdef __CYGWIN__
#include <windows.h>
#include <sys/cygwin.h>
#endif

#include "ruby/ruby.h"
#include "ruby/io.h"
#include "ruby/util.h"
#include "dln.h"
#include "internal.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#else
int flock(int, int);
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifndef MAXPATHLEN
# define MAXPATHLEN 1024
#endif

#include <ctype.h>

#include <time.h>

#ifdef HAVE_UTIME_H
#include <utime.h>
#elif defined HAVE_SYS_UTIME_H
#include <sys/utime.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif

#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif

#if !defined HAVE_LSTAT && !defined lstat
#define lstat stat
#endif

/* define system APIs */
#ifdef _WIN32
#define STAT(p, s)	rb_w32_ustati64((p), (s))
#undef lstat
#define lstat(p, s)	rb_w32_ustati64((p), (s))
#undef access
#define access(p, m)	rb_w32_uaccess((p), (m))
#undef chmod
#define chmod(p, m)	rb_w32_uchmod((p), (m))
#undef chown
#define chown(p, o, g)	rb_w32_uchown((p), (o), (g))
#undef utime
#define utime(p, t)	rb_w32_uutime((p), (t))
#undef link
#define link(f, t)	rb_w32_ulink((f), (t))
#undef unlink
#define unlink(p)	rb_w32_uunlink(p)
#undef rename
#define rename(f, t)	rb_w32_urename((f), (t))
#else
#define STAT(p, s)	stat((p), (s))
#endif

#define rb_sys_fail_path(path) rb_sys_fail_str(path)

#if defined(__BEOS__) || defined(__HAIKU__) /* should not change ID if -1 */
static int
be_chown(const char *path, uid_t owner, gid_t group)
{
    if (owner == (uid_t)-1 || group == (gid_t)-1) {
        struct stat st;
        if (STAT(path, &st) < 0) return -1;
        if (owner == (uid_t)-1) owner = st.st_uid;
        if (group == (gid_t)-1) group = st.st_gid;
    }
    return chown(path, owner, group);
}
#define chown be_chown
static int
be_fchown(int fd, uid_t owner, gid_t group)
{
    if (owner == (uid_t)-1 || group == (gid_t)-1) {
        struct stat st;
        if (fstat(fd, &st) < 0) return -1;
        if (owner == (uid_t)-1) owner = st.st_uid;
        if (group == (gid_t)-1) group = st.st_gid;
    }
    return fchown(fd, owner, group);
}
#define fchown be_fchown
#endif /* __BEOS__ || __HAIKU__ */


static mrb_value
file_path_convert(mrb_value name)
{
#ifndef _WIN32 /* non Windows == Unix */
    //rb_encoding *fname_encoding = rb_enc_from_index(ENCODING_GET(name));
    //rb_encoding *fs_encoding;
    //if (rb_default_internal_encoding() != NULL
	//    && rb_usascii_encoding() != fname_encoding
	//    && rb_ascii8bit_encoding() != fname_encoding
	//    && (fs_encoding = rb_filesystem_encoding()) != fname_encoding
	//    && !rb_enc_str_asciionly_p(name)) {
    //    /* Don't call rb_filesystem_encoding() before US-ASCII and ASCII-8BIT */
    //    /* fs_encoding should be ascii compatible */
    //    name = mrb_str_conv_enc(name, fname_encoding, fs_encoding);
    //}
#endif
    return name;
}



mrb_value rb_cFile;
mrb_value rb_mFileTest;
mrb_value rb_cStat;
void
mrb_file_gem_init(mrb_state* mrb) {
  _class_cextension = mrb_define_module(mrb, "CRubyExtension");
  mrb_define_class_method(mrb, _class_cextension, "c_method", mrb_c_method, ARGS_NONE());
}
