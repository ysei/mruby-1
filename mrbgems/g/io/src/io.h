/**********************************************************************

  rubyio.h -

  $Author: kosaki $
  created at: Fri Nov 12 16:47:09 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

#ifndef RUBY_IO_Hrb_io
#define RUBY_IO_H 1

#if defined(__cplusplus)
extern "C" {
#if 0
} /* satisfy cc-mode */
#endif
#endif

#include <stdio.h>
#include <errno.h>
#include "encoding.h"
#include "error.h"

#if defined(HAVE_STDIO_EXT_H)
#include <stdio_ext.h>
#endif

#include "ruby/config.h"
#if defined(HAVE_POLL)
#  include <poll.h>
#  define mrb_WAITFD_IN  POLLIN
#  define mrb_WAITFD_PRI POLLPRI
#  define mrb_WAITFD_OUT POLLOUT
#else
#  define mrb_WAITFD_IN  0x001
#  define mrb_WAITFD_PRI 0x002
#  define mrb_WAITFD_OUT 0x004
#endif

#if defined __GNUC__ && __GNUC__ >= 4
#pragma GCC visibility push(default)
#endif

typedef struct {
    char *ptr;                  /* off + len <= capa */
    int off;
    int len;
    int capa;
} mrb_io_buffer_t;

typedef struct mrb_io_t {
    int fd;                     /* file descriptor */
    FILE *stdio_file;		/* stdio ptr for read/write if available */
    int mode;			/* mode flags: FMODE_XXXs */
    mrb_int pid;		/* child's pid (for pipes) */
    int lineno;			/* number of lines read */
    mrb_value pathv;		/* pathname for file */
    void (*finalize)(struct mrb_io_t*,int); /* finalize proc */

    mrb_io_buffer_t wbuf, rbuf;

    mrb_value tied_io_for_writing;

    /*
     * enc  enc2 read action                      write action
     * NULL NULL force_encoding(default_external) write the byte sequence of str
     * e1   NULL force_encoding(e1)               convert str.encoding to e1
     * e1   e2   convert from e2 to e1            convert str.encoding to e2
     */
    struct mrb_io_enc_t {
        mrb_encoding *enc;
        mrb_encoding *enc2;
        int ecflags;
        mrb_value ecopts;
    } encs;

    mrb_econv_t *readconv;
    mrb_io_buffer_t cbuf;

    mrb_econv_t *writeconv;
    mrb_value writeconv_asciicompat;
    int writeconv_pre_ecflags;
    mrb_value writeconv_pre_ecopts;
    int writeconv_initialized;

    mrb_value write_lock;
} mrb_io_t;

#define HAVE_RB_IO_T 1

#define FMODE_READABLE              0x00000001
#define FMODE_WRITABLE              0x00000002
#define FMODE_READWRITE             (FMODE_READABLE|FMODE_WRITABLE)
#define FMODE_BINMODE               0x00000004
#define FMODE_SYNC                  0x00000008
#define FMODE_TTY                   0x00000010
#define FMODE_DUPLEX                0x00000020
#define FMODE_APPEND                0x00000040
#define FMODE_CREATE                0x00000080
/* #define FMODE_NOREVLOOKUP        0x00000100 */
#define FMODE_WSPLIT                0x00000200
#define FMODE_WSPLIT_INITIALIZED    0x00000400
#define FMODE_TRUNC                 0x00000800
#define FMODE_TEXTMODE              0x00001000
/* #define FMODE_PREP               0x00010000 */
#define FMODE_SETENC_BY_BOM         0x00100000

#define GetOpenFile(obj,fp) mrb_io_check_closed((fp) = RFILE(mrb_io_taint_check(obj))->fptr)

#define mrb_IO_BUFFER_INIT(buf) do {\
    (buf).ptr = NULL;\
    (buf).off = 0;\
    (buf).len = 0;\
    (buf).capa = 0;\
} while (0)

#define MakeOpenFile(obj, fp) do {\
    if (RFILE(obj)->fptr) {\
	rb_io_close(obj);\
	rb_io_fptr_finalize(RFILE(obj)->fptr);\
	RFILE(obj)->fptr = 0;\
    }\
    (fp) = 0;\
    mrb_IO_FPTR_NEW(fp);\
    RFILE(obj)->fptr = (fp);\
} while (0)

#define mrb_IO_FPTR_NEW(fp) do {\
    (fp) = ALLOC(mrb_io_t);\
    (fp)->fd = -1;\
    (fp)->stdio_file = NULL;\
    (fp)->mode = 0;\
    (fp)->pid = 0;\
    (fp)->lineno = 0;\
    (fp)->pathv = Qnil;\
    (fp)->finalize = 0;\
    mrb_IO_BUFFER_INIT((fp)->wbuf);\
    mrb_IO_BUFFER_INIT((fp)->rbuf);\
    mrb_IO_BUFFER_INIT((fp)->cbuf);\
    (fp)->readconv = NULL;\
    (fp)->writeconv = NULL;\
    (fp)->writeconv_asciicompat = Qnil;\
    (fp)->writeconv_pre_ecflags = 0;\
    (fp)->writeconv_pre_ecopts = Qnil;\
    (fp)->writeconv_initialized = 0;\
    (fp)->tied_io_for_writing = 0;\
    (fp)->encs.enc = NULL;\
    (fp)->encs.enc2 = NULL;\
    (fp)->encs.ecflags = 0;\
    (fp)->encs.ecopts = Qnil;\
    (fp)->write_lock = 0;\
} while (0)

FILE *rb_io_stdio_file(mrb_state *mr,mrb_io_t *fptr);

FILE *rb_fdopen(mrb_state *mr,int, const char*);
int mrb_io_modestr_fmode(mrb_state *mr,const char *modestr);
int mrb_io_modestr_oflags(mrb_state *mr,const char *modestr);
int mrb_io_oflags_fmode(mrb_state *mr,int oflags);
void mrb_io_check_writable(mrb_state *mr,mrb_io_t*);
void mrb_io_check_readable(mrb_state *mr,mrb_io_t*);
void mrb_io_check_char_readable(mrb_state *mr,mrb_io_t *fptr);
void mrb_io_check_byte_readable(mrb_state *mr,mrb_io_t *fptr);
int mrb_io_fptr_finalize(mrb_state *mr,mrb_io_t*);
void mrb_io_synchronized(mrb_state *mr,mrb_io_t*);
void mrb_io_check_initialized(mrb_state *mr,mrb_io_t*);
void mrb_io_check_closed(mrb_state *mr,mrb_io_t*);
mrb_value mrb_io_get_io(mrb_state *mr,mrb_value io);
mrb_value mrb_io_get_write_io(mrb_state *mr,mrb_value io);
mrb_value mrb_io_set_write_io(mrb_state *mr,mrb_value io, mrb_value w);
int mrb_io_wait_readable(mrb_state *mr,int);
int mrb_io_wait_writable(mrb_state *mr,int);
int mrb_wait_for_single_fd(mrb_state *mr,int fd, int events, struct timeval *tv);
void mrb_io_set_nonblock(mrb_state *mr,mrb_io_t *fptr);
int mrb_io_extract_encoding_option(mrb_state *mr,mrb_value opt, mrb_encoding **enc_p, mrb_encoding **enc2_p, int *fmode_p);
ssize_t mrb_io_bufwrite(mrb_state *mr,mrb_value io, const void *buf, size_t size);

/* compatibility for ruby 1.8 and older */
#define mrb_io_mode_flags(modestr) mrb_io_modestr_fmode(modestr)
#define mrb_io_modenum_flags(oflags) mrb_io_oflags_fmode(oflags)

mrb_value mrb_io_taint_check(mrb_state *mr,mrb_value);
void mrb_eof_error(mrb_state *mr);

void mrb_io_read_check(mrb_state *mr,mrb_io_t*);
int mrb_io_read_pending(mrb_state *mr,mrb_io_t*);
//DEPRECATED(void mrb_read_check(FILE*));

#if defined __GNUC__ && __GNUC__ >= 4
#pragma GCC visibility pop
#endif

#if defined(__cplusplus)
#if 0
{ /* satisfy cc-mode */
#endif
}  /* extern "C" { */
#endif

#endif /* RUBY_IO_H */
