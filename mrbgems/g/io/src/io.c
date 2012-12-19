/**********************************************************************

  io.c -

  $Author: usa $
  created at: Fri Oct 15 18:08:59 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "mruby.h"
#include "io.h"
//#include "dln.h"
//#include "internal.h"
#include <ctype.h>
#include <errno.h>

#define free(x) xfree(x)

#if defined(DOSISH) || defined(__CYGWIN__)
#include <io.h>
#endif

#include <sys/types.h>
#if defined HAVE_NET_SOCKET_H
# include <net/socket.h>
#elif defined HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif

#if defined(__BOW__) || defined(__CYGWIN__) || defined(_WIN32) || defined(__EMX__) || defined(__BEOS__) || defined(__HAIKU__)
# define NO_SAFE_RENAME
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(sun) || defined(_nec_ews)
# define USE_SETVBUF
#endif

#ifdef __QNXNTO__
#include "unix.h"
#endif

#include <sys/types.h>
#if defined(HAVE_SYS_IOCTL_H) && !defined(_WIN32)
#include <sys/ioctl.h>
#endif
#if defined(HAVE_FCNTL_H) || defined(_WIN32)
#include <fcntl.h>
#elif defined(HAVE_SYS_FCNTL_H)
#include <sys/fcntl.h>
#endif

#if !HAVE_OFF_T && !defined(off_t)
# define off_t  long
#endif

#include <sys/stat.h>

/* EMX has sys/param.h, but.. */
#if defined(HAVE_SYS_PARAM_H) && !(defined(__EMX__) || defined(__HIUX_MPP__))
# include <sys/param.h>
#endif

#if !defined NOFILE
# define NOFILE 64
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYSCALL_H
#include <syscall.h>
#elif defined HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif

#if defined(__BEOS__) || defined(__HAIKU__)
# ifndef NOFILE
#  define NOFILE (OPEN_MAX)
# endif
#endif

#include "ruby/util.h"

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

#if SIZEOF_OFF_T > SIZEOF_LONG && !defined(HAVE_LONG_LONG)
# error off_t is bigger than long, but you have no long long...
#endif

#ifndef PIPE_BUF
# ifdef _POSIX_PIPE_BUF
#  define PIPE_BUF _POSIX_PIPE_BUF
# else
#  define PIPE_BUF 512 /* is this ok? */
# endif
#endif

#define numberof(array) (int)(sizeof(array) / sizeof((array)[0]))

#define IO_RBUF_CAPA_MIN  8192
#define IO_CBUF_CAPA_MIN  (128*1024)
#define IO_RBUF_CAPA_FOR(fptr) (NEED_READCONV(fptr) ? IO_CBUF_CAPA_MIN : IO_RBUF_CAPA_MIN)
#define IO_WBUF_CAPA_MIN  8192

/* define system APIs */
#ifdef _WIN32
#undef open
#define open	mrb_w32_uopen
#endif

mrb_value mrb_cIO;
mrb_value mrb_eEOFError;
mrb_value mrb_eIOError;
mrb_value mrb_mWaitReadable;
mrb_value mrb_mWaitWritable;

mrb_value mrb_stdin, mrb_stdout, mrb_stderr;
mrb_value mrb_deferr;		/* rescue VIM plugin */
static mrb_value orig_stdout, orig_stderr;

mrb_value mrb_output_fs;
mrb_value mrb_rs;
mrb_value mrb_output_rs;
mrb_value mrb_default_rs;

static mrb_value argf;

static mrb_sym id_write, id_read, id_getc, id_flush, id_readpartial, id_set_encoding;
static mrb_value sym_mode, sym_perm, sym_extenc, sym_intenc, sym_encoding, sym_open_args;
static mrb_value sym_textmode, sym_binmode, sym_autoclose;

struct argf {
    mrb_value filename, current_file;
    long last_lineno;		/* $. */
    long lineno;
    mrb_value argv;
    char *inplace;
    struct mrb_io_enc_t encs;
    int8_t init_p, next_p, binmode;
};

static int max_file_descriptor = NOFILE;
void
mrb_update_max_fd(mrb_state *mrb, int fd)
{
    struct stat buf;
    if (fstat(fd, &buf) != 0 && errno == EBADF) {
mrb_bug( "mrb_update_max_fd: invalid fd (%d) given.", fd);
    }
    if (max_file_descriptor < fd) max_file_descriptor = fd;
}

void
mrb_maygvl_fd_fix_cloexec(mrb_state *mrb, int fd)
{
  /* MinGW don't have F_GETFD and FD_CLOEXEC.  [ruby-core:40281] */
#ifdef F_GETFD
  int flags, flags2, ret;
  flags = fcntl(fd, F_GETFD); /* should not fail except EBADF. */
  if (flags == -1) {
mrb_error( "mrb_maygvl_fd_fix_cloexec: fcntl(%d, F_GETFD) failed: %s", fd, strerror(errno));
  }
  if (fd <= 2)
    flags2 = flags & ~FD_CLOEXEC; /* Clear CLOEXEC for standard file descriptors: 0, 1, 2. */
  else
    flags2 = flags | FD_CLOEXEC; /* Set CLOEXEC for non-standard file descriptors: 3, 4, 5, ... */
  if (flags != flags2) {
    ret = fcntl(fd, F_SETFD, flags2);
    if (ret == -1) {
mrb_bug( "mrb_maygvl_fd_fix_cloexec: fcntl(%d, F_SETFD, %d) failed: %s", fd, flags2, strerror(errno));
    }
  }
#endif
}

int
mrb_cloexec_fcntl_dupfd(mrb_state *mrb, int fd, int minfd)
{
    int ret;

#if defined(HAVE_FCNTL) && defined(F_DUPFD_CLOEXEC)
    static int try_dupfd_cloexec = 1;
    if (try_dupfd_cloexec) {
      ret = fcntl(fd, F_DUPFD_CLOEXEC, minfd);
      if (ret != -1) {
	if (ret <= 2)
	  mrb_maygvl_fd_fix_cloexec(mrb,ret);
	return ret;
      }
      /* F_DUPFD_CLOEXEC is available since Linux 2.6.24.  Linux 2.6.18 fails with EINVAL */
      if (errno == EINVAL) {
	ret = fcntl(fd, F_DUPFD, minfd);
	if (ret != -1) {
	  try_dupfd_cloexec = 0;
	}
      }
    }
    else {
      ret = fcntl(fd, F_DUPFD, minfd);
    }
#elif defined(F_DUPFD)
    ret = fcntl(fd, F_DUPFD, minfd);
#else
    ret = -1;
    errno = EINVAL;
#endif
    if (ret == -1) return -1;
mrb_maygvl_fd_fix_cloexec(mrb, ret);
    return ret;
}

#define argf_of(obj) (*(struct argf *)DATA_PTR(obj))
#define ARGF argf_of(argf)

#ifdef _STDIO_USES_IOSTREAM  /* GNU libc */
#  ifdef _IO_fpos_t
#    define STDIO_READ_DATA_PENDING(fp) ((fp)->_IO_read_ptr != (fp)->_IO_read_end)
#  else
#    define STDIO_READ_DATA_PENDING(fp) ((fp)->_gptr < (fp)->_egptr)
#  endif
#elif defined(FILE_COUNT)
#  define STDIO_READ_DATA_PENDING(fp) ((fp)->FILE_COUNT > 0)
#elif defined(FILE_READEND)
#  define STDIO_READ_DATA_PENDING(fp) ((fp)->FILE_READPTR < (fp)->FILE_READEND)
#elif defined(__BEOS__) || defined(__HAIKU__)
#  define STDIO_READ_DATA_PENDING(fp) ((fp)->_state._eof == 0)
#else
#  define STDIO_READ_DATA_PENDING(fp) (!feof(fp))
#endif

#define GetWriteIO(io) mrb_io_get_write_io(io)

#define READ_DATA_PENDING(fptr) ((fptr)->rbuf.len)
#define READ_DATA_PENDING_COUNT(fptr) ((fptr)->rbuf.len)
#define READ_DATA_PENDING_PTR(fptr) ((fptr)->rbuf.ptr+(fptr)->rbuf.off)
#define READ_DATA_BUFFERED(fptr) READ_DATA_PENDING(fptr)

#define READ_CHAR_PENDING(fptr) ((fptr)->cbuf.len)
#define READ_CHAR_PENDING_COUNT(fptr) ((fptr)->cbuf.len)
#define READ_CHAR_PENDING_PTR(fptr) ((fptr)->cbuf.ptr+(fptr)->cbuf.off)

#if defined(_WIN32)
#define WAIT_FD_IN_WIN32(fptr) \
(mrb_state *mrb, mrb_w32_io_cancelable_p((fptr)->fd) ? 0 : mrb_thread_wait_fd((fptr)->fd))
#else
#define WAIT_FD_IN_WIN32(fptr)
#endif

#define READ_CHECK(fptr) do {\
    if (!READ_DATA_PENDING(fptr)) {\
	WAIT_FD_IN_WIN32(mrb_state *mrb, fptr);\
	mrb_io_check_closed(mrb_state *mrb, fptr);\
     }\
} while(0)

#ifndef S_ISSOCK
#  ifdef _S_ISSOCK
#    define S_ISSOCK(m) _S_ISSOCK(m)
#  else
#    ifdef _S_IFSOCK
#      define S_ISSOCK(m) (((m) & S_IFMT) == _S_IFSOCK)
#    else
#      ifdef S_IFSOCK
#	 define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#      endif
#    endif
#  endif
#endif

#define mrb_sys_fail_path(path) mrb_sys_fail_str(path)

static int io_fflush(mrb_io_t *);
static mrb_io_t *flush_before_seek(mrb_io_t *fptr);

#define NEED_NEWLINE_DECORATOR_ON_READ(fptr) ((fptr)->mode & FMODE_TEXTMODE)
#define NEED_NEWLINE_DECORATOR_ON_WRITE(fptr) ((fptr)->mode & FMODE_TEXTMODE)
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
/* Windows */
# define DEFAULT_TEXTMODE FMODE_TEXTMODE
# define TEXTMODE_NEWLINE_DECORATOR_ON_WRITE ECONV_CRLF_NEWLINE_DECORATOR
/*
 * CRLF newline is set as default newline decorator.
 * If only CRLF newline conversion is needed, we use binary IO process
 * with OS's text mode for IO performance improvement.
 * If encoding conversion is needed or a user sets text mode, we use encoding
 * conversion IO process and universal newline decorator by default.
 */
#define NEED_READCONV(fptr) ((fptr)->encs.enc2 != NULL || (fptr)->encs.ecflags & ~ECONV_CRLF_NEWLINE_DECORATOR)
#define NEED_WRITECONV(fptr) (((fptr)->encs.enc != NULL && (fptr)->encs.enc != mrb_ascii8bit_encoding()) || ((fptr)->encs.ecflags & ((ECONV_DECORATOR_MASK & ~ECONV_CRLF_NEWLINE_DECORATOR)|ECONV_STATEFUL_DECORATOR_MASK)))
#define SET_BINARY_MODE(fptr) setmode((fptr)->fd, O_BINARY)

#define NEED_NEWLINE_DECORATOR_ON_READ_CHECK(fptr) do {\
    if (NEED_NEWLINE_DECORATOR_ON_READ(fptr)) {\
	if (((fptr)->mode & FMODE_READABLE) &&\
	    !((fptr)->encs.ecflags & ECONV_NEWLINE_DECORATOR_MASK)) {\
	    setmode((fptr)->fd, O_BINARY);\
	}\
	else {\
	    setmode((fptr)->fd, O_TEXT);\
	}\
    }\
} while(0)

#define SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(enc2, ecflags) do {\
    if ((enc2) && ((ecflags) & ECONV_DEFAULT_NEWLINE_DECORATOR)) {\
	(mrb_state *mrb, ecflags) |= ECONV_UNIVERSAL_NEWLINE_DECORATOR;\
    }\
} while(0)

/*
 * IO unread with taking care of removed '\r' in text mode.
 */
static void
io_unread(mrb_state *mrb, mrb_io_t *fptr)
{
    off_t r, pos;
    ssize_t read_size;
    long i;
    long newlines = 0;
    long extra_max;
    char *p;
    char *buf;

mrb_io_check_closed(mrb_state *mrb, fptr);
    if (fptr->rbuf.len == 0 || fptr->mode & FMODE_DUPLEX) {
	return;
    }

    errno = 0;
    if (!mrb_w32_fd_is_text(fptr->fd)) {
	r = lseek(fptr->fd, -fptr->rbuf.len, SEEK_CUR);
	if (r < 0 && errno) {
	    if (errno == ESPIPE)
		fptr->mode |= FMODE_DUPLEX;
	    return;
	}

	fptr->rbuf.off = 0;
	fptr->rbuf.len = 0;
	return;
    }

    pos = lseek(fptr->fd, 0, SEEK_CUR);
    if (pos < 0 && errno) {
	if (errno == ESPIPE)
	    fptr->mode |= FMODE_DUPLEX;
	return;
    }

    /* add extra offset for removed '\r' in rbuf */
    extra_max = (long)(pos - fptr->rbuf.len);
    p = fptr->rbuf.ptr + fptr->rbuf.off;

    /* if the end of rbuf is '\r', rbuf doesn't have '\r' within rbuf.len */
    if (*(fptr->rbuf.ptr + fptr->rbuf.capa - 1) == '\r') {
	newlines++;
    }

    for (i = 0; i < fptr->rbuf.len; i++) {
	if (*p == '\n') newlines++;
	if (extra_max == newlines) break;
	p++;
    }

    buf = ALLOC_N(char, fptr->rbuf.len + newlines);
    while (newlines >= 0) {
	r = lseek(fptr->fd, pos - fptr->rbuf.len - newlines, SEEK_SET);
	if (newlines == 0) break;
	if (r < 0) {
	    newlines--;
	    continue;
	}
	read_size = _read(fptr->fd, buf, fptr->rbuf.len + newlines);
	if (read_size < 0) {
	    free(buf);
	    mrb_sys_fail_path(fptr->pathv);
	}
	if (read_size == fptr->rbuf.len) {
	    lseek(fptr->fd, r, SEEK_SET);
	    break;
	}
	else {
	    newlines--;
	}
    }
free(mrb_state *mrb, buf);
    fptr->rbuf.off = 0;
    fptr->rbuf.len = 0;
    return;
}

/*
 * We use io_seek to back cursor position when changing mode from text to binary,
 * but stdin and pipe cannot seek back. Stdin and pipe read should use encoding
 * conversion for working properly with mode change.
 *
 * Return previous translation mode.
 */
static inline int
set_binary_mode_with_seek_cur(mrb_state *mrb, mrb_io_t *fptr)
{
    if (!mrb_w32_fd_is_text(fptr->fd)) return O_BINARY;

    if (fptr->rbuf.len == 0 || fptr->mode & FMODE_DUPLEX) {
	return setmode(fptr->fd, O_BINARY);
    }
flush_before_seek(mrb_state *mrb, fptr);
    return setmode(fptr->fd, O_BINARY);
}
#define SET_BINARY_MODE_WITH_SEEK_CUR(fptr) set_binary_mode_with_seek_cur(fptr)

#else
/* Unix */
# define DEFAULT_TEXTMODE 0
#define NEED_READCONV(fptr) ((fptr)->encs.enc2 != NULL || NEED_NEWLINE_DECORATOR_ON_READ(fptr))
#define NEED_WRITECONV(fptr) (((fptr)->encs.enc != NULL && (fptr)->encs.enc != mrb_ascii8bit_encoding()) || NEED_NEWLINE_DECORATOR_ON_WRITE(fptr) || ((fptr)->encs.ecflags & (ECONV_DECORATOR_MASK|ECONV_STATEFUL_DECORATOR_MASK)))
#define SET_BINARY_MODE(fptr) (void)(fptr)
#define NEED_NEWLINE_DECORATOR_ON_READ_CHECK(fptr) (void)(fptr)
#define SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(enc2, ecflags) ((void)(enc2), (void)(ecflags))
#define SET_BINARY_MODE_WITH_SEEK_CUR(fptr) (void)(fptr)
#endif

#if !defined HAVE_SHUTDOWN && !defined shutdown
#define shutdown(a,b)	0
#endif

#if defined(_WIN32)
#define is_socket(fd, path)	mrb_w32_is_socket(fd)
#elif !defined(S_ISSOCK)
#define is_socket(fd, path)	0
#else
static int
is_socket(mrb_state *mrb, int fd, mrb_value path)
{
    struct stat sbuf;
    if (fstat(fd, &sbuf) < 0)
        mrb_sys_fail(mrb,path.value.p);
    return S_ISSOCK(sbuf.st_mode);
}
#endif

void
mrb_eof_error(mrb_state *mrb)
{
mrb_raise(mrb,mrb->object_class, "end of file reached");
}

mrb_value
mrb_io_taint_check(mrb_state *mrb, mrb_value io)
{
   // if (!OBJ_UNTRUSTED(io) && mrb_safe_level() >= 4)
	//mrb_raise(mrb_state *mrb, mrb_eSecurityError, "Insecure: operation on trusted IO");
//mrb_check_frozen(mrb_state *mrb, io);
    return io;
}

void
mrb_io_check_initialized(mrb_state *mrb, mrb_io_t *fptr)
{
    if (!fptr) {
	mrb_raise(mrb, mrb->object_class, "uninitialized stream");
    }
}

void
mrb_io_check_closed(mrb_state *mrb, mrb_io_t *fptr)
{
    mrb_io_check_initialized(mrb,fptr);
    if (fptr->fd < 0) {
	mrb_raise(mrb, mrb->object_class, "closed stream");
    }
}


mrb_value
mrb_io_get_io(mrb_state *mrb, mrb_value io)
{
    return mrb_convert_type(mrb,io, MRB_TT_FILE, "IO", "to_io");
}

static mrb_value
mrb_io_check_io(mrb_state *mrb, mrb_value io)
{
    return mrb_check_convert_type(io, MRB_TT_FILE, "IO", "to_io");
}

mrb_value
mrb_io_get_write_io(mrb_state *mrb, mrb_value io)
{
    mrb_value write_io;
    mrb_io_check_initialized(mrb, RFILE(io)->fptr);
    write_io = RFILE(io)->fptr->tied_io_for_writing;
    if (write_io) {
        return write_io;
    }
    return io;
}

mrb_value
mrb_io_set_write_io(mrb_state *mrb, mrb_value io, mrb_value w)
{
    mrb_value write_io;
    mrb_io_check_initialized(mrb, RFILE(io)->fptr);
    if (!RTEST(w)) {
        w = mrb_fixnum_value(0);
    }
    else {
        GetWriteIO(mrb, w);
    }
    write_io = RFILE(io)->fptr->tied_io_for_writing;
RFILE(mrb_state *mrb, io)->fptr->tied_io_for_writing = w;
    return write_io ? write_io : Qnil;
}

/*
 *  call-seq:
 *     IO.try_convert(obj)  ->  io or nil
 *
 *  Try to convert <i>obj</i> into an IO, using to_io method.
 *  Returns converted IO or nil if <i>obj</i> cannot be converted
 *  for any reason.
 *
 *     IO.try_convert(STDOUT)     #=> STDOUT
 *     IO.try_convert("STDOUT")   #=> nil
 *
 *     require 'zlib'
 *     f = open("/tmp/zz.gz")       #=> #<File:/tmp/zz.gz>
 *     z = Zlib::GzipReader.open(f) #=> #<Zlib::GzipReader:0x81d8744>
 *     IO.try_convert(z)            #=> #<File:/tmp/zz.gz>
 *
 */
static mrb_value
mrb_io_s_try_convert(mrb_state *mrb, mrb_value dummy, mrb_value io)
{
    return mrb_io_check_io(io);
}

#if !(defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32))
static void
io_unread(mrb_state *mrb, mrb_io_t *fptr)
{
    off_t r;
mrb_io_check_closed(mrb_state *mrb, fptr);
    if (fptr->rbuf.len == 0 || fptr->mode & FMODE_DUPLEX)
        return;
    /* xxx: target position may be negative if buffer is filled by ungetc */
    errno = 0;
    r = lseek(fptr->fd, -fptr->rbuf.len, SEEK_CUR);
    if (r < 0 && errno) {
        if (errno == ESPIPE)
            fptr->mode |= FMODE_DUPLEX;
        return;
    }
    fptr->rbuf.off = 0;
    fptr->rbuf.len = 0;
    return;
}
#endif

static mrb_encoding *io_input_encoding(mrb_io_t *fptr);

static void
io_ungetbyte(mrb_state *mrb, mrb_value str, mrb_io_t *fptr)
{
    long len = RSTRING_LEN(str);

    if (fptr->rbuf.ptr == NULL) {
        const int min_capa = IO_RBUF_CAPA_FOR(fptr);
        fptr->rbuf.off = 0;
        fptr->rbuf.len = 0;
#if SIZEOF_LONG > SIZEOF_INT
	if (len > INT_MAX)
	    mrb_raise(mrb_eIOError, "ungetbyte failed");
#endif
	if (len > min_capa)
	    fptr->rbuf.capa = (int)len;
	else
	    fptr->rbuf.capa = min_capa;
        fptr->rbuf.ptr = ALLOC_N(char, fptr->rbuf.capa);
    }
    if (fptr->rbuf.capa < len + fptr->rbuf.len) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "ungetbyte failed");
    }
    if (fptr->rbuf.off < len) {
MEMMOVE(mrb_state *mrb, fptr->rbuf.ptr+fptr->rbuf.capa-fptr->rbuf.len,
                fptr->rbuf.ptr+fptr->rbuf.off,
                char, fptr->rbuf.len);
        fptr->rbuf.off = fptr->rbuf.capa-fptr->rbuf.len;
    }
fptr->rbuf.off-=(mrb_state *mrb, int)len;
fptr->rbuf.len+=(mrb_state *mrb, int)len;
MEMMOVE(mrb_state *mrb, fptr->rbuf.ptr+fptr->rbuf.off, RSTRING_PTR(str), char, len);
}

static mrb_io_t *
flush_before_seek(mrb_state *mrb, mrb_io_t *fptr)
{
    if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
io_unread(mrb_state *mrb, fptr);
    errno = 0;
    return fptr;
}

#define io_seek(fptr, ofs, whence) (errno = 0, lseek(flush_before_seek(fptr)->fd, (ofs), (whence)))
#define io_tell(fptr) lseek(flush_before_seek(fptr)->fd, 0, SEEK_CUR)

#ifndef SEEK_CUR
# define SEEK_SET 0
# define SEEK_CUR 1
# define SEEK_END 2
#endif

#define FMODE_SYNCWRITE (FMODE_SYNC|FMODE_WRITABLE)

void
mrb_io_check_char_readable(mrb_state *mrb, mrb_io_t *fptr)
{
mrb_io_check_closed(mrb_state *mrb, fptr);
    if (!(fptr->mode & FMODE_READABLE)) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "not opened for reading");
    }
    if (fptr->wbuf.len) {
        if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
    }
    if (fptr->tied_io_for_writing) {
	mrb_io_t *wfptr;
	GetOpenFile(mrb_state *mrb, fptr->tied_io_for_writing, wfptr);
        if (io_fflush(wfptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
    }
}

void
mrb_io_check_byte_readable(mrb_state *mrb, mrb_io_t *fptr)
{
mrb_io_check_char_readable(mrb_state *mrb, fptr);
    if (READ_CHAR_PENDING(fptr)) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "byte oriented read for character buffered IO");
    }
}

void
mrb_io_check_readable(mrb_state *mrb, mrb_io_t *fptr)
{
mrb_io_check_byte_readable(mrb_state *mrb, fptr);
}

static mrb_encoding*
io_read_encoding(mrb_state *mrb, mrb_io_t *fptr)
{
    if (fptr->encs.enc) {
	return fptr->encs.enc;
    }
    return mrb_default_external_encoding();
}

static mrb_encoding*
io_input_encoding(mrb_state *mrb, mrb_io_t *fptr)
{
    if (fptr->encs.enc2) {
	return fptr->encs.enc2;
    }
    return io_read_encoding(fptr);
}

void
mrb_io_check_writable(mrb_state *mrb, mrb_io_t *fptr)
{
mrb_io_check_closed(mrb_state *mrb, fptr);
    if (!(fptr->mode & FMODE_WRITABLE)) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "not opened for writing");
    }
    if (fptr->rbuf.len) {
io_unread(mrb_state *mrb, fptr);
    }
}

int
mrb_io_read_pending(mrb_state *mrb, mrb_io_t *fptr)
{
    /* This function is used for bytes and chars.  Confusing. */
    if (READ_CHAR_PENDING(fptr))
        return 1; /* should raise? */
    return READ_DATA_PENDING(fptr);
}

void
mrb_read_check(mrb_state *mrb, FILE *fp)
{
    if (!STDIO_READ_DATA_PENDING(fp)) {
	mrb_thread_wait_fd(mrb_state *mrb, fileno(fp));
    }
}

void
mrb_io_read_check(mrb_state *mrb, mrb_io_t *fptr)
{
    if (!READ_DATA_PENDING(fptr)) {
	mrb_thread_wait_fd(mrb_state *mrb, fptr->fd);
    }
    return;
}

static int
ruby_dup(mrb_state *mrb, int orig)
{
    int fd;

    fd = dup(orig);
    if (fd < 0) {
	if (errno == EMFILE || errno == ENFILE || errno == ENOMEM) {
	    mrb_gc();
	    fd = dup(orig);
	}
	if (fd < 0) {
	    mrb_sys_fail(0);
	}
    }
mrb_update_max_fd(mrb_state *mrb, fd);
    return fd;
}

static mrb_value
io_alloc(mrb_state *mrb, mrb_value klass)
{
NEWOBJ(mrb_state *mrb, io, struct RFile);
OBJSETUP(mrb_state *mrb, io, klass, T_FILE);

    io->fptr = 0;

    return (mrb_value)io;
}

#ifndef S_ISREG
#   define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

static int
wsplit_p(mrb_state *mrb, mrb_io_t *fptr)
{
#if defined(HAVE_FCNTL) && defined(F_GETFL) && defined(O_NONBLOCK)
    int r;
#endif

    if (!(fptr->mode & FMODE_WSPLIT_INITIALIZED)) {
        struct stat buf;
        if (fstat(fptr->fd, &buf) == 0 &&
!S_ISREG(mrb_state *mrb, buf.st_mode)
#if defined(HAVE_FCNTL) && defined(F_GETFL) && defined(O_NONBLOCK)
            && (r = fcntl(fptr->fd, F_GETFL)) != -1 &&
!(mrb_state *mrb, r & O_NONBLOCK)
#endif
            ) {
            fptr->mode |= FMODE_WSPLIT;
        }
        fptr->mode |= FMODE_WSPLIT_INITIALIZED;
    }
    return fptr->mode & FMODE_WSPLIT;
}

struct io_internal_read_struct {
    int fd;
    void *buf;
    size_t capa;
};

struct io_internal_write_struct {
    int fd;
    const void *buf;
    size_t capa;
};

static mrb_value
internal_read_func(mrb_state *mrb, void *ptr)
{
    struct io_internal_read_struct *iis = ptr;
    return read(iis->fd, iis->buf, iis->capa);
}

static mrb_value
internal_write_func(mrb_state *mrb, void *ptr)
{
    struct io_internal_write_struct *iis = ptr;
    return write(iis->fd, iis->buf, iis->capa);
}

static ssize_t
mrb_read_internal(mrb_state *mrb, int fd, void *buf, size_t count)
{
    struct io_internal_read_struct iis;
    iis.fd = fd;
    iis.buf = buf;
    iis.capa = count;

    return (ssize_t)mrb_thread_io_blocking_region(internal_read_func, &iis, fd);
}

static ssize_t
mrb_write_internal(mrb_state *mrb, int fd, const void *buf, size_t count)
{
    struct io_internal_write_struct iis;
    iis.fd = fd;
    iis.buf = buf;
    iis.capa = count;

    return (ssize_t)mrb_thread_io_blocking_region(internal_write_func, &iis, fd);
}

static long
io_writable_length(mrb_state *mrb, mrb_io_t *fptr, long l)
{
    if (PIPE_BUF < l &&
!mrb_thread_alone(mrb_state *mrb, ) &&
wsplit_p(mrb_state *mrb, fptr)) {
        l = PIPE_BUF;
    }
    return l;
}

static mrb_value
io_flush_buffer_sync(mrb_state *mrb, void *arg)
{
    mrb_io_t *fptr = arg;
    long l = io_writable_length(fptr, fptr->wbuf.len);
    ssize_t r = write(fptr->fd, fptr->wbuf.ptr+fptr->wbuf.off, (size_t)l);

    if (fptr->wbuf.len <= r) {
	fptr->wbuf.off = 0;
	fptr->wbuf.len = 0;
	return 0;
    }
    if (0 <= r) {
	fptr->wbuf.off += (int)r;
	fptr->wbuf.len -= (int)r;
	errno = EAGAIN;
    }
    return (mrb_value)-1;
}

static mrb_value
io_flush_buffer_async(mrb_state *mrb, mrb_value arg)
{
    mrb_io_t *fptr = (mrb_io_t *)arg;
    return mrb_thread_io_blocking_region(io_flush_buffer_sync, fptr, fptr->fd);
}

static inline int
io_flush_buffer(mrb_state *mrb, mrb_io_t *fptr)
{
    if (fptr->write_lock) {
	return (int)mrb_mutex_synchronize(fptr->write_lock, io_flush_buffer_async, (mrb_value)fptr);
    }
    else {
	return (int)io_flush_buffer_async((mrb_value)fptr);
    }
}

static int
io_fflush(mrb_state *mrb, mrb_io_t *fptr)
{
mrb_io_check_closed(mrb_state *mrb, fptr);
    if (fptr->wbuf.len == 0)
        return 0;
    if (!mrb_thread_fd_writable(fptr->fd)) {
mrb_io_check_closed(mrb_state *mrb, fptr);
    }
    while (fptr->wbuf.len > 0 && io_flush_buffer(fptr) != 0) {
	if (!mrb_io_wait_writable(fptr->fd))
	    return -1;
mrb_io_check_closed(mrb_state *mrb, fptr);
    }
    return 0;
}

int
mrb_io_wait_readable(mrb_state *mrb, int f)
{
    if (f < 0) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "closed stream");
    }
    switch (errno) {
      case EINTR:
#if defined(ERESTART)
      case ERESTART:
#endif
	mrb_thread_wait_fd(mrb_state *mrb, f);
	return TRUE;

      case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
      case EWOULDBLOCK:
#endif
	mrb_wait_for_single_fd(mrb_state *mrb, f, RB_WAITFD_IN, NULL);
	return TRUE;

      default:
	return FALSE;
    }
}

int
mrb_io_wait_writable(mrb_state *mrb, int f)
{
    if (f < 0) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "closed stream");
    }
    switch (errno) {
      case EINTR:
#if defined(ERESTART)
      case ERESTART:
#endif
	mrb_thread_fd_writable(mrb_state *mrb, f);
	return TRUE;

      case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
      case EWOULDBLOCK:
#endif
	mrb_wait_for_single_fd(mrb_state *mrb, f, RB_WAITFD_OUT, NULL);
	return TRUE;

      default:
	return FALSE;
    }
}

static void
make_writeconv(mrb_state *mrb, mrb_io_t *fptr)
{
    if (!fptr->writeconv_initialized) {
        const char *senc, *denc;
        mrb_encoding *enc;
        int ecflags;
        mrb_value ecopts;

        fptr->writeconv_initialized = 1;

        ecflags = fptr->encs.ecflags & ~ECONV_NEWLINE_DECORATOR_READ_MASK;
        ecopts = fptr->encs.ecopts;

        if (!fptr->encs.enc || (fptr->encs.enc == mrb_ascii8bit_encoding() && !fptr->encs.enc2)) {
            /* no encoding conversion */
            fptr->writeconv_pre_ecflags = 0;
            fptr->writeconv_pre_ecopts = Qnil;
            fptr->writeconv = mrb_econv_open_opts("", "", ecflags, ecopts);
            if (!fptr->writeconv)
mrb_exc_raise(mrb_state *mrb, mrb_econv_open_exc("", "", ecflags));
            fptr->writeconv_asciicompat = Qnil;
        }
        else {
            enc = fptr->encs.enc2 ? fptr->encs.enc2 : fptr->encs.enc;
            senc = mrb_econv_asciicompat_encoding(mrb_enc_name(enc));
            if (!senc && !(fptr->encs.ecflags & ECONV_STATEFUL_DECORATOR_MASK)) {
                /* single conversion */
                fptr->writeconv_pre_ecflags = ecflags;
                fptr->writeconv_pre_ecopts = ecopts;
                fptr->writeconv = NULL;
                fptr->writeconv_asciicompat = Qnil;
            }
            else {
                /* double conversion */
                fptr->writeconv_pre_ecflags = ecflags & ~ECONV_STATEFUL_DECORATOR_MASK;
                fptr->writeconv_pre_ecopts = ecopts;
                if (senc) {
                    denc = mrb_enc_name(enc);
                    fptr->writeconv_asciicompat = mrb_str_new2(senc);
                }
                else {
                    senc = denc = "";
                    fptr->writeconv_asciicompat = mrb_str_new2(mrb_enc_name(enc));
                }
                ecflags = fptr->encs.ecflags & (ECONV_ERROR_HANDLER_MASK|ECONV_STATEFUL_DECORATOR_MASK);
                ecopts = fptr->encs.ecopts;
                fptr->writeconv = mrb_econv_open_opts(senc, denc, ecflags, ecopts);
                if (!fptr->writeconv)
mrb_exc_raise(mrb_state *mrb, mrb_econv_open_exc(senc, denc, ecflags));
            }
        }
    }
}

/* writing functions */
struct binwrite_arg {
    mrb_io_t *fptr;
    mrb_value str;
    const char *ptr;
    long length;
};

struct write_arg {
    mrb_value io;
    mrb_value str;
    int nosync;
};

static mrb_value
io_binwrite_string(mrb_state *mrb, mrb_value arg)
{
    struct binwrite_arg *p = (struct binwrite_arg *)arg;
    long l = io_writable_length(p->fptr, p->length);
    return mrb_write_internal(p->fptr->fd, p->ptr, l);
}

static long
io_binwrite(mrb_state *mrb, mrb_value str, const char *ptr, long len, mrb_io_t *fptr, int nosync)
{
    long n, r, offset = 0;

    if ((n = len) <= 0) return n;
    if (fptr->wbuf.ptr == NULL && !(!nosync && (fptr->mode & FMODE_SYNC))) {
        fptr->wbuf.off = 0;
        fptr->wbuf.len = 0;
        fptr->wbuf.capa = IO_WBUF_CAPA_MIN;
        fptr->wbuf.ptr = ALLOC_N(char, fptr->wbuf.capa);
	fptr->write_lock = mrb_mutex_new();
    }
    if ((!nosync && (fptr->mode & (FMODE_SYNC|FMODE_TTY))) ||
(mrb_state *mrb, fptr->wbuf.ptr && fptr->wbuf.capa <= fptr->wbuf.len + len)) {
	struct binwrite_arg arg;

        /* xxx: use writev to avoid double write if available */
        if (fptr->wbuf.len && fptr->wbuf.len+len <= fptr->wbuf.capa) {
            if (fptr->wbuf.capa < fptr->wbuf.off+fptr->wbuf.len+len) {
MEMMOVE(mrb_state *mrb, fptr->wbuf.ptr, fptr->wbuf.ptr+fptr->wbuf.off, char, fptr->wbuf.len);
                fptr->wbuf.off = 0;
            }
MEMMOVE(mrb_state *mrb, fptr->wbuf.ptr+fptr->wbuf.off+fptr->wbuf.len, ptr+offset, char, len);
            fptr->wbuf.len += (int)len;
            n = 0;
        }
        if (io_fflush(fptr) < 0)
            return -1L;
        if (n == 0)
            return len;
        /* avoid context switch between "a" and "\n" in STDERR.puts "a".
           [ruby-dev:25080] */
	if (fptr->stdio_file != stderr && !mrb_thread_fd_writable(fptr->fd)) {
	    mrb_io_check_closed(fptr);
	}
	arg.fptr = fptr;
	arg.str = str;
      retry:
	arg.ptr = ptr + offset;
	arg.length = n;
	if (fptr->write_lock) {
	    r = mrb_mutex_synchronize(fptr->write_lock, io_binwrite_string, (mrb_value)&arg);
	}
	else {
	    long l = io_writable_length(fptr, n);
	    r = mrb_write_internal(fptr->fd, ptr+offset, l);
	}
	/* xxx: other threads may modify given string. */
        if (r == n) return len;
        if (0 <= r) {
            offset += r;
            n -= r;
            errno = EAGAIN;
        }
        if (mrb_io_wait_writable(fptr->fd)) {
mrb_io_check_closed(mrb_state *mrb, fptr);
	    if (offset < len)
		goto retry;
        }
        return -1L;
    }

    if (fptr->wbuf.off) {
        if (fptr->wbuf.len)
MEMMOVE(mrb_state *mrb, fptr->wbuf.ptr, fptr->wbuf.ptr+fptr->wbuf.off, char, fptr->wbuf.len);
        fptr->wbuf.off = 0;
    }
MEMMOVE(mrb_state *mrb, fptr->wbuf.ptr+fptr->wbuf.off+fptr->wbuf.len, ptr+offset, char, len);
    fptr->wbuf.len += (int)len;
    return len;
}

# define MODE_BTMODE(a,b,c) ((fmode & FMODE_BINMODE) ? (b) : \
(mrb_state *mrb, fmode & FMODE_TEXTMODE) ? (c) : (a))
static mrb_value
do_writeconv(mrb_state *mrb, mrb_value str, mrb_io_t *fptr)
{
    if (NEED_WRITECONV(fptr)) {
        mrb_value common_encoding = Qnil;
	SET_BINARY_MODE(mrb_state *mrb, fptr);

make_writeconv(mrb_state *mrb, fptr);

        if (fptr->writeconv) {
#define fmode (fptr->mode)
            if (!NIL_P(fptr->writeconv_asciicompat))
                common_encoding = fptr->writeconv_asciicompat;
            else if (MODE_BTMODE(DEFAULT_TEXTMODE,0,1) && !mrb_enc_asciicompat(mrb_enc_get(str))) {
mrb_raise(mrb_state *mrb, mrb_eArgError, "ASCII incompatible string written for text mode IO without encoding conversion: %s",
mrb_enc_name(mrb_state *mrb, mrb_enc_get(str)));
            }
#undef fmode
        }
        else {
            if (fptr->encs.enc2)
                common_encoding = mrb_enc_from_encoding(fptr->encs.enc2);
            else if (fptr->encs.enc != mrb_ascii8bit_encoding())
                common_encoding = mrb_enc_from_encoding(fptr->encs.enc);
        }

        if (!NIL_P(common_encoding)) {
            str = mrb_str_encode(str, common_encoding,
                fptr->writeconv_pre_ecflags, fptr->writeconv_pre_ecopts);
        }

        if (fptr->writeconv) {
            str = mrb_econv_str_convert(fptr->writeconv, str, ECONV_PARTIAL_INPUT);
        }
    }
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
#define fmode (fptr->mode)
    else if (MODE_BTMODE(DEFAULT_TEXTMODE,0,1)) {
	if ((fptr->mode & FMODE_READABLE) &&
	    !(fptr->encs.ecflags & ECONV_NEWLINE_DECORATOR_MASK)) {
	    setmode(fptr->fd, O_BINARY);
	}
	else {
	    setmode(fptr->fd, O_TEXT);
	}
	if (!mrb_enc_asciicompat(mrb_enc_get(str))) {
	    mrb_raise(mrb_eArgError, "ASCII incompatible string written for text mode IO without encoding conversion: %s",
	    mrb_enc_name(mrb_enc_get(str)));
        }
    }
#undef fmode
#endif
    return str;
}

static long
io_fwrite(mrb_state *mrb, mrb_value str, mrb_io_t *fptr, int nosync)
{
#ifdef _WIN32
    if (fptr->mode & FMODE_TTY) {
	long len = mrb_w32_write_console(str, fptr->fd);
	if (len > 0) return len;
    }
#endif
    str = do_writeconv(str, fptr);
    return io_binwrite(str, RSTRING_PTR(str), RSTRING_LEN(str),
		       fptr, nosync);
}

ssize_t
mrb_io_bufwrite(mrb_state *mrb, mrb_value io, const void *buf, size_t size)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_writable(mrb_state *mrb, fptr);
    return (ssize_t)io_binwrite(0, buf, (long)size, fptr, 0);
}

static mrb_value
io_write(mrb_state *mrb, mrb_value io, mrb_value str, int nosync)
{
    mrb_io_t *fptr;
    long n;
    mrb_value tmp;

mrb_secure(4);
    io = GetWriteIO(io);
    str = mrb_obj_as_string(str);
    tmp = mrb_io_check_io(io);
    if (NIL_P(tmp)) {
	/* port is not IO, call write method for it. */
	return mrb_funcall(io, id_write, 1, str);
    }
    io = tmp;
    if (RSTRING_LEN(str) == 0) return INT2FIX(0);

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_writable(mrb_state *mrb, fptr);

    n = io_fwrite(str, fptr, nosync);
    if (n == -1L) mrb_sys_fail_path(fptr->pathv);

    return LONG2FIX(n);
}

/*
 *  call-seq:
 *     ios.write(string)    -> integer
 *
 *  Writes the given string to <em>ios</em>. The stream must be opened
 *  for writing. If the argument is not a string, it will be converted
 *  to a string using <code>to_s</code>. Returns the number of bytes
 *  written.
 *
 *     count = $stdout.write("This is a test\n")
 *     puts "That was #{count} bytes of data"
 *
 *  <em>produces:</em>
 *
 *     This is a test
 *     That was 15 bytes of data
 */

static mrb_value
io_write_m(mrb_state *mrb, mrb_value io, mrb_value str)
{
    return io_write(io, str, 0);
}

mrb_value
mrb_io_write(mrb_state *mrb, mrb_value io, mrb_value str)
{
    return mrb_funcall(io, id_write, 1, str);
}

/*
 *  call-seq:
 *     ios << obj     -> ios
 *
 *  String Output---Writes <i>obj</i> to <em>ios</em>.
 *  <i>obj</i> will be converted to a string using
 *  <code>to_s</code>.
 *
 *     $stdout << "Hello " << "world!\n"
 *
 *  <em>produces:</em>
 *
 *     Hello world!
 */


mrb_value
mrb_io_addstr(mrb_state *mrb, mrb_value io, mrb_value str)
{
mrb_io_write(mrb_state *mrb, io, str);
    return io;
}

/*
 *  call-seq:
 *     ios.flush    -> ios
 *
 *  Flushes any buffered data within <em>ios</em> to the underlying
 *  operating system (note that this is Ruby internal buffering only;
 *  the OS may buffer the data as well).
 *
 *     $stdout.print "no newline"
 *     $stdout.flush
 *
 *  <em>produces:</em>
 *
 *     no newline
 */

mrb_value
mrb_io_flush(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

    if (TYPE(io) != T_FILE) {
        return mrb_funcall(io, id_flush, 0);
    }

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);

    if (fptr->mode & FMODE_WRITABLE) {
        if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
#ifdef _WIN32
        if (GetFileType((HANDLE)mrb_w32_get_osfhandle(fptr->fd)) == FILE_TYPE_DISK) {
	    fsync(fptr->fd);
        }
#endif
    }
    if (fptr->mode & FMODE_READABLE) {
io_unread(mrb_state *mrb, fptr);
    }

    return io;
}

/*
 *  call-seq:
 *     ios.pos     -> integer
 *     ios.tell    -> integer
 *
 *  Returns the current offset (in bytes) of <em>ios</em>.
 *
 *     f = File.new("testfile")
 *     f.pos    #=> 0
 *     f.gets   #=> "This is line one\n"
 *     f.pos    #=> 17
 */

static mrb_value
mrb_io_tell(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    off_t pos;

GetOpenFile(mrb_state *mrb, io, fptr);
    pos = io_tell(fptr);
    if (pos < 0 && errno) mrb_sys_fail_path(fptr->pathv);
    pos -= fptr->rbuf.len;
    return OFFT2NUM(pos);
}

static mrb_value
mrb_io_seek(mrb_state *mrb, mrb_value io, mrb_value offset, int whence)
{
    mrb_io_t *fptr;
    off_t pos;

    pos = NUM2OFFT(offset);
GetOpenFile(mrb_state *mrb, io, fptr);
    pos = io_seek(fptr, pos, whence);
    if (pos < 0 && errno) mrb_sys_fail_path(fptr->pathv);

    return INT2FIX(0);
}

/*
 *  call-seq:
 *     ios.seek(amount, whence=IO::SEEK_SET)  ->  0
 *
 *  Seeks to a given offset <i>anInteger</i> in the stream according to
 *  the value of <i>whence</i>:
 *
 *    IO::SEEK_CUR  | Seeks to _amount_ plus current position
 *    --------------+----------------------------------------------------
 *    IO::SEEK_END  | Seeks to _amount_ plus end of stream (you probably
 *                  | want a negative value for _amount_)
 *    --------------+----------------------------------------------------
 *    IO::SEEK_SET  | Seeks to the absolute location given by _amount_
 *
 *  Example:
 *
 *     f = File.new("testfile")
 *     f.seek(-13, IO::SEEK_END)   #=> 0
 *     f.readline                  #=> "And so on...\n"
 */

static mrb_value
mrb_io_seek_m(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value offset, ptrname;
    int whence = SEEK_SET;

    if (mrb_scan_args(argc, argv, "11", &offset, &ptrname) == 2) {
	whence = NUM2INT(ptrname);
    }

    return mrb_io_seek(io, offset, whence);
}

/*
 *  call-seq:
 *     ios.pos = integer    -> integer
 *
 *  Seeks to the given position (in bytes) in <em>ios</em>.
 *
 *     f = File.new("testfile")
 *     f.pos = 17
 *     f.gets   #=> "This is line two\n"
 */

static mrb_value
mrb_io_set_pos(mrb_state *mrb, mrb_value io, mrb_value offset)
{
    mrb_io_t *fptr;
    off_t pos;

    pos = NUM2OFFT(offset);
GetOpenFile(mrb_state *mrb, io, fptr);
    pos = io_seek(fptr, pos, SEEK_SET);
    if (pos < 0 && errno) mrb_sys_fail_path(fptr->pathv);

    return OFFT2NUM(pos);
}

static void clear_readconv(mrb_io_t *fptr);

/*
 *  call-seq:
 *     ios.rewind    -> 0
 *
 *  Positions <em>ios</em> to the beginning of input, resetting
 *  <code>lineno</code> to zero.
 *
 *     f = File.new("testfile")
 *     f.readline   #=> "This is line one\n"
 *     f.rewind     #=> 0
 *     f.lineno     #=> 0
 *     f.readline   #=> "This is line one\n"
 *
 *  Note that it cannot be used with streams such as pipes, ttys, and sockets.
 */

static mrb_value
mrb_io_rewind(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
    if (io_seek(fptr, 0L, 0) < 0 && errno) mrb_sys_fail_path(fptr->pathv);
#ifdef _WIN32
    if (GetFileType((HANDLE)mrb_w32_get_osfhandle(fptr->fd)) == FILE_TYPE_DISK) {
	fsync(mrb_state *mrb, fptr->fd);
    }
#endif
    if (io == ARGF.current_file) {
	ARGF.lineno -= fptr->lineno;
    }
    fptr->lineno = 0;
    if (fptr->readconv) {
	clear_readconv(mrb_state *mrb, fptr);
    }

    return INT2FIX(0);
}

static int
io_fillbuf(mrb_state *mrb, mrb_io_t *fptr)
{
    ssize_t r;

    if (fptr->rbuf.ptr == NULL) {
        fptr->rbuf.off = 0;
        fptr->rbuf.len = 0;
        fptr->rbuf.capa = IO_RBUF_CAPA_FOR(fptr);
        fptr->rbuf.ptr = ALLOC_N(char, fptr->rbuf.capa);
#ifdef _WIN32
	fptr->rbuf.capa--;
#endif
    }
    if (fptr->rbuf.len == 0) {
      retry:
	{
	    r = mrb_read_internal(fptr->fd, fptr->rbuf.ptr, fptr->rbuf.capa);
	}
        if (r < 0) {
            if (mrb_io_wait_readable(fptr->fd))
                goto retry;
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
        }
        fptr->rbuf.off = 0;
        fptr->rbuf.len = (int)r; /* r should be <= rbuf_capa */
        if (r == 0)
            return -1; /* EOF */
    }
    return 0;
}

/*
 *  call-seq:
 *     ios.eof     -> true or false
 *     ios.eof?    -> true or false
 *
 *  Returns true if <em>ios</em> is at end of file that means
 *  there are no more data to read.
 *  The stream must be opened for reading or an <code>IOError</code> will be
 *  raised.
 *
 *     f = File.new("testfile")
 *     dummy = f.readlines
 *     f.eof   #=> true
 *
 *  If <em>ios</em> is a stream such as pipe or socket, <code>IO#eof?</code>
 *  blocks until the other end sends some data or closes it.
 *
 *     r, w = IO.pipe
 *     Thread.new { sleep 1; w.close }
 *     r.eof?  #=> true after 1 second blocking
 *
 *     r, w = IO.pipe
 *     Thread.new { sleep 1; w.puts "a" }
 *     r.eof?  #=> false after 1 second blocking
 *
 *     r, w = IO.pipe
 *     r.eof?  # blocks forever
 *
 *  Note that <code>IO#eof?</code> reads data to the input byte buffer.
 *  So <code>IO#sysread</code> may not behave as you intend with
 *  <code>IO#eof?</code>, unless you call <code>IO#rewind</code>
 *  first (which is not available for some streams).
 */

mrb_value
mrb_io_eof(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);

    if (READ_CHAR_PENDING(fptr)) return Qfalse;
    if (READ_DATA_PENDING(fptr)) return Qfalse;
READ_CHECK(mrb_state *mrb, fptr);
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
    if (!NEED_READCONV(fptr) && NEED_NEWLINE_DECORATOR_ON_READ(fptr)) {
	return eof(fptr->fd) ? Qtrue : Qfalse;
    }
#endif
    if (io_fillbuf(fptr) < 0) {
	return Qtrue;
    }
    return Qfalse;
}

/*
 *  call-seq:
 *     ios.sync    -> true or false
 *
 *  Returns the current ``sync mode'' of <em>ios</em>. When sync mode is
 *  true, all output is immediately flushed to the underlying operating
 *  system and is not buffered by Ruby internally. See also
 *  <code>IO#fsync</code>.
 *
 *     f = File.new("testfile")
 *     f.sync   #=> false
 */

static mrb_value
mrb_io_sync(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);
    return (fptr->mode & FMODE_SYNC) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     ios.sync = boolean   -> boolean
 *
 *  Sets the ``sync mode'' to <code>true</code> or <code>false</code>.
 *  When sync mode is true, all output is immediately flushed to the
 *  underlying operating system and is not buffered internally. Returns
 *  the new state. See also <code>IO#fsync</code>.
 *
 *     f = File.new("testfile")
 *     f.sync = true
 *
 *  <em>(produces no output)</em>
 */

static mrb_value
mrb_io_set_sync(mrb_state *mrb, mrb_value io, mrb_value sync)
{
    mrb_io_t *fptr;

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);
    if (RTEST(sync)) {
	fptr->mode |= FMODE_SYNC;
    }
    else {
	fptr->mode &= ~FMODE_SYNC;
    }
    return sync;
}

#ifdef HAVE_FSYNC
static mrb_value nogvl_fsync(void *ptr)
{
    mrb_io_t *fptr = ptr;

    return (mrb_value)fsync(fptr->fd);
}

/*
 *  call-seq:
 *     ios.fsync   -> 0 or nil
 *
 *  Immediately writes all buffered data in <em>ios</em> to disk.
 *  Note that <code>fsync</code> differs from
 *  using <code>IO#sync=</code>. The latter ensures that data is flushed
 *  from Ruby's buffers, but doesn't not guarantee that the underlying
 *  operating system actually writes it to disk.
 *
 *  <code>NotImplementedError</code> is raised
 *  if the underlying operating system does not support <em>fsync(2)</em>.
 */

static mrb_value
mrb_io_fsync(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);

    if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
#ifndef _WIN32	/* already called in io_fflush() */
    if ((int)mrb_thread_io_blocking_region(nogvl_fsync, fptr, fptr->fd) < 0)
	mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
#endif
    return INT2FIX(0);
}
#else
#define mrb_io_fsync mrb_f_notimplement
#endif

#ifdef HAVE_FDATASYNC
static mrb_value nogvl_fdatasync(void *ptr)
{
    mrb_io_t *fptr = ptr;

    return (mrb_value)fdatasync(fptr->fd);
}

/*
 *  call-seq:
 *     ios.fdatasync   -> 0 or nil
 *
 *  Immediately writes all buffered data in <em>ios</em> to disk.
 *
 *  If the underlying operating system does not support <em>fdatasync(2)</em>,
 *  <code>IO#fsync</code> is called instead (which might raise a
 *  <code>NotImplementedError</code>).
 */

static mrb_value
mrb_io_fdatasync(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);

    if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);

    if ((int)mrb_thread_io_blocking_region(nogvl_fdatasync, fptr, fptr->fd) == 0)
	return INT2FIX(0);

    /* fall back */
    return mrb_io_fsync(io);
}
#else
#define mrb_io_fdatasync mrb_io_fsync
#endif

/*
 *  call-seq:
 *     ios.fileno    -> fixnum
 *     ios.to_i      -> fixnum
 *
 *  Returns an integer representing the numeric file descriptor for
 *  <em>ios</em>.
 *
 *     $stdin.fileno    #=> 0
 *     $stdout.fileno   #=> 1
 */

static mrb_value
mrb_io_fileno(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    int fd;

GetOpenFile(mrb_state *mrb, io, fptr);
    fd = fptr->fd;
    return INT2FIX(fd);
}


/*
 *  call-seq:
 *     ios.pid    -> fixnum
 *
 *  Returns the process ID of a child process associated with
 *  <em>ios</em>. This will be set by <code>IO.popen</code>.
 *
 *     pipe = IO.popen("-")
 *     if pipe
 *       $stderr.puts "In parent, child pid is #{pipe.pid}"
 *     else
 *       $stderr.puts "In child, pid is #{$$}"
 *     end
 *
 *  <em>produces:</em>
 *
 *     In child, pid is 26209
 *     In parent, child pid is 26209
 */

static mrb_value
mrb_io_pid(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
    if (!fptr->pid)
	return Qnil;
    return PIDT2NUM(fptr->pid);
}


/*
 * call-seq:
 *   ios.inspect   -> string
 *
 * Return a string describing this IO object.
 */

static mrb_value
mrb_io_inspect(mrb_state *mrb, mrb_value obj)
{
    mrb_io_t *fptr;
    mrb_value result;
    static const char closed[] = " (closed)";

    fptr = RFILE(mrb_io_taint_check(obj))->fptr;
    if (!fptr) return mrb_any_to_s(obj);
    result = mrb_str_new_cstr("#<");
mrb_str_append(mrb_state *mrb, result, mrb_class_name(CLASS_OF(obj)));
mrb_str_cat2(mrb_state *mrb, result, ":");
    if (NIL_P(fptr->pathv)) {
        if (fptr->fd < 0) {
	    mrb_str_cat(result, closed+1, strlen(closed)-1);
        }
        else {
	    mrb_str_catf(result, "fd %d", fptr->fd);
        }
    }
    else {
	mrb_str_append(mrb_state *mrb, result, fptr->pathv);
        if (fptr->fd < 0) {
	    mrb_str_cat(result, closed, strlen(closed));
        }
    }
    return mrb_str_cat2(result, ">");
}

/*
 *  call-seq:
 *     ios.to_io  ->  ios
 *
 *  Returns <em>ios</em>.
 */

static mrb_value
mrb_io_to_io(mrb_state *mrb, mrb_value io)
{
    return io;
}

/* reading functions */
static long
read_buffered_data(mrb_state *mrb, char *ptr, long len, mrb_io_t *fptr)
{
    int n;

    n = READ_DATA_PENDING_COUNT(fptr);
    if (n <= 0) return 0;
    if (n > len) n = (int)len;
MEMMOVE(mrb_state *mrb, ptr, fptr->rbuf.ptr+fptr->rbuf.off, char, n);
    fptr->rbuf.off += n;
    fptr->rbuf.len -= n;
    return n;
}

static long
io_bufread(mrb_state *mrb, char *ptr, long len, mrb_io_t *fptr)
{
    long offset = 0;
    long n = len;
    long c;

    if (READ_DATA_PENDING(fptr) == 0) {
	while (n > 0) {
          again:
	    c = mrb_read_internal(fptr->fd, ptr+offset, n);
	    if (c == 0) break;
	    if (c < 0) {
                if (mrb_io_wait_readable(fptr->fd))
                    goto again;
		return -1;
	    }
	    offset += c;
	    if ((n -= c) <= 0) break;
	    mrb_thread_wait_fd(fptr->fd);
	}
	return len - n;
    }

    while (n > 0) {
	c = read_buffered_data(ptr+offset, n, fptr);
	if (c > 0) {
	    offset += c;
	    if ((n -= c) <= 0) break;
	}
	mrb_thread_wait_fd(mrb_state *mrb, fptr->fd);
	mrb_io_check_closed(mrb_state *mrb, fptr);
	if (io_fillbuf(fptr) < 0) {
	    break;
	}
    }
    return len - n;
}

static long
io_fread(mrb_state *mrb, mrb_value str, long offset, mrb_io_t *fptr)
{
    long len;

mrb_str_locktmp(mrb_state *mrb, str);
    len = io_bufread(RSTRING_PTR(str) + offset, RSTRING_LEN(str) - offset,
		     fptr);
mrb_str_unlocktmp(mrb_state *mrb, str);
    if (len < 0) mrb_sys_fail_path(fptr->pathv);
    return len;
}

ssize_t
mrb_io_bufread(mrb_state *mrb, mrb_value io, void *buf, size_t size)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_readable(mrb_state *mrb, fptr);
    return (ssize_t)io_bufread(buf, (long)size, fptr);
}

#define SMALLBUF 100

static long
remain_size(mrb_state *mrb, mrb_io_t *fptr)
{
    struct stat st;
    off_t siz = READ_DATA_PENDING_COUNT(fptr);
    off_t pos;

    if (fstat(fptr->fd, &st) == 0  && S_ISREG(st.st_mode)
#if defined(__BEOS__) || defined(__HAIKU__)
	&& (st.st_dev > 3)
#endif
	)
    {
        if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
	pos = lseek(fptr->fd, 0, SEEK_CUR);
	if (st.st_size >= pos && pos >= 0) {
	    siz += st.st_size - pos;
	    if (siz > LONG_MAX) {
		mrb_raise(mrb_state *mrb, mrb_eIOError, "file too big for single read");
	    }
	}
    }
    else {
	siz += BUFSIZ;
    }
    return (long)siz;
}

static mrb_value
io_enc_str(mrb_state *mrb, mrb_value str, mrb_io_t *fptr)
{
OBJ_TAINT(mrb_state *mrb, str);
mrb_enc_associate(mrb_state *mrb, str, io_read_encoding(fptr));
    return str;
}

static void
make_readconv(mrb_state *mrb, mrb_io_t *fptr, int size)
{
    if (!fptr->readconv) {
        int ecflags;
        mrb_value ecopts;
        const char *sname, *dname;
        ecflags = fptr->encs.ecflags & ~ECONV_NEWLINE_DECORATOR_WRITE_MASK;
        ecopts = fptr->encs.ecopts;
        if (fptr->encs.enc2) {
            sname = mrb_enc_name(fptr->encs.enc2);
            dname = mrb_enc_name(fptr->encs.enc);
        }
        else {
            sname = dname = "";
        }
        fptr->readconv = mrb_econv_open_opts(sname, dname, ecflags, ecopts);
        if (!fptr->readconv)
mrb_exc_raise(mrb_state *mrb, mrb_econv_open_exc(sname, dname, ecflags));
        fptr->cbuf.off = 0;
        fptr->cbuf.len = 0;
	if (size < IO_CBUF_CAPA_MIN) size = IO_CBUF_CAPA_MIN;
        fptr->cbuf.capa = size;
        fptr->cbuf.ptr = ALLOC_N(char, fptr->cbuf.capa);
    }
}

#define MORE_CHAR_SUSPENDED Qtrue
#define MORE_CHAR_FINISHED Qnil
static mrb_value
fill_cbuf(mrb_state *mrb, mrb_io_t *fptr, int ec_flags)
{
    const unsigned char *ss, *sp, *se;
    unsigned char *ds, *dp, *de;
    mrb_econv_result_t res;
    int putbackable;
    int cbuf_len0;
    mrb_value exc;

    ec_flags |= ECONV_PARTIAL_INPUT;

    if (fptr->cbuf.len == fptr->cbuf.capa)
        return MORE_CHAR_SUSPENDED; /* cbuf full */
    if (fptr->cbuf.len == 0)
        fptr->cbuf.off = 0;
    else if (fptr->cbuf.off + fptr->cbuf.len == fptr->cbuf.capa) {
memmove(mrb_state *mrb, fptr->cbuf.ptr, fptr->cbuf.ptr+fptr->cbuf.off, fptr->cbuf.len);
        fptr->cbuf.off = 0;
    }

    cbuf_len0 = fptr->cbuf.len;

    while (1) {
        ss = sp = (const unsigned char *)fptr->rbuf.ptr + fptr->rbuf.off;
        se = sp + fptr->rbuf.len;
        ds = dp = (unsigned char *)fptr->cbuf.ptr + fptr->cbuf.off + fptr->cbuf.len;
        de = (unsigned char *)fptr->cbuf.ptr + fptr->cbuf.capa;
        res = mrb_econv_convert(fptr->readconv, &sp, se, &dp, de, ec_flags);
        fptr->rbuf.off += (int)(sp - ss);
        fptr->rbuf.len -= (int)(sp - ss);
        fptr->cbuf.len += (int)(dp - ds);

        putbackable = mrb_econv_putbackable(fptr->readconv);
        if (putbackable) {
mrb_econv_putback(mrb_state *mrb, fptr->readconv, (unsigned char *)fptr->rbuf.ptr + fptr->rbuf.off - putbackable, putbackable);
            fptr->rbuf.off -= putbackable;
            fptr->rbuf.len += putbackable;
        }

        exc = mrb_econv_make_exception(fptr->readconv);
        if (!NIL_P(exc))
            return exc;

        if (cbuf_len0 != fptr->cbuf.len)
            return MORE_CHAR_SUSPENDED;

        if (res == econv_finished) {
            return MORE_CHAR_FINISHED;
	}

        if (res == econv_source_buffer_empty) {
            if (fptr->rbuf.len == 0) {
		READ_CHECK(mrb_state *mrb, fptr);
                if (io_fillbuf(fptr) == -1) {
		    if (!fptr->readconv) {
			return MORE_CHAR_FINISHED;
		    }
                    ds = dp = (unsigned char *)fptr->cbuf.ptr + fptr->cbuf.off + fptr->cbuf.len;
                    de = (unsigned char *)fptr->cbuf.ptr + fptr->cbuf.capa;
                    res = mrb_econv_convert(fptr->readconv, NULL, NULL, &dp, de, 0);
                    fptr->cbuf.len += (int)(dp - ds);
mrb_econv_check_error(mrb_state *mrb, fptr->readconv);
		    break;
                }
            }
        }
    }
    if (cbuf_len0 != fptr->cbuf.len)
	return MORE_CHAR_SUSPENDED;

    return MORE_CHAR_FINISHED;
}

static mrb_value
more_char(mrb_state *mrb, mrb_io_t *fptr)
{
    mrb_value v;
    v = fill_cbuf(fptr, ECONV_AFTER_OUTPUT);
    if (v != MORE_CHAR_SUSPENDED && v != MORE_CHAR_FINISHED)
mrb_exc_raise(mrb_state *mrb, v);
    return v;
}

static mrb_value
io_shift_cbuf(mrb_state *mrb, mrb_io_t *fptr, int len, mrb_value *strp)
{
    mrb_value str = Qnil;
    if (strp) {
	str = *strp;
	if (NIL_P(str)) {
	    *strp = str = mrb_str_new(fptr->cbuf.ptr+fptr->cbuf.off, len);
	}
	else {
	    mrb_str_cat(str, fptr->cbuf.ptr+fptr->cbuf.off, len);
	}
	OBJ_TAINT(mrb_state *mrb, str);
	mrb_enc_associate(mrb_state *mrb, str, fptr->encs.enc);
    }
    fptr->cbuf.off += len;
    fptr->cbuf.len -= len;
    /* xxx: set coderange */
    if (fptr->cbuf.len == 0)
        fptr->cbuf.off = 0;
    else if (fptr->cbuf.capa/2 < fptr->cbuf.off) {
memmove(mrb_state *mrb, fptr->cbuf.ptr, fptr->cbuf.ptr+fptr->cbuf.off, fptr->cbuf.len);
        fptr->cbuf.off = 0;
    }
    return str;
}

static void
io_setstrbuf(mrb_state *mrb, mrb_value *str,long len)
{
#ifdef _WIN32
    if (NIL_P(*str)) {
	*str = mrb_str_new(0, len+1);
	mrb_str_set_len(mrb_state *mrb, *str,len);
    }
    else {
	StringValue(mrb_state *mrb, *str);
	mrb_str_modify(mrb_state *mrb, *str);
	mrb_str_resize(mrb_state *mrb, *str, len+1);
	mrb_str_set_len(mrb_state *mrb, *str,len);
    }
#else
    if (NIL_P(*str)) {
	*str = mrb_str_new(0, len);
    }
    else {
	StringValue(mrb_state *mrb, *str);
	mrb_str_modify(mrb_state *mrb, *str);
	mrb_str_resize(mrb_state *mrb, *str, len);
    }
#endif
}

static mrb_value
read_all(mrb_state *mrb, mrb_io_t *fptr, long siz, mrb_value str)
{
    long bytes;
    long n;
    long pos;
    mrb_encoding *enc;
    int cr;

    if (NEED_READCONV(fptr)) {
	SET_BINARY_MODE(mrb_state *mrb, fptr);
	io_setstrbuf(mrb_state *mrb, &str,0);
make_readconv(mrb_state *mrb, fptr, 0);
        while (1) {
            mrb_value v;
            if (fptr->cbuf.len) {
io_shift_cbuf(mrb_state *mrb, fptr, fptr->cbuf.len, &str);
            }
            v = fill_cbuf(fptr, 0);
            if (v != MORE_CHAR_SUSPENDED && v != MORE_CHAR_FINISHED) {
                if (fptr->cbuf.len) {
io_shift_cbuf(mrb_state *mrb, fptr, fptr->cbuf.len, &str);
                }
mrb_exc_raise(mrb_state *mrb, v);
            }
            if (v == MORE_CHAR_FINISHED) {
clear_readconv(mrb_state *mrb, fptr);
                return io_enc_str(str, fptr);
            }
        }
    }

NEED_NEWLINE_DECORATOR_ON_READ_CHECK(mrb_state *mrb, fptr);
    bytes = 0;
    pos = 0;

    enc = io_read_encoding(fptr);
    cr = 0;

    if (siz == 0) siz = BUFSIZ;
io_setstrbuf(mrb_state *mrb, &str,siz);
    for (;;) {
	READ_CHECK(mrb_state *mrb, fptr);
	n = io_fread(str, bytes, fptr);
	if (n == 0 && bytes == 0) {
	    break;
	}
	bytes += n;
	if (cr != ENC_CODERANGE_BROKEN)
	    pos += mrb_str_coderange_scan_restartable(RSTRING_PTR(str) + pos, RSTRING_PTR(str) + bytes, enc, &cr);
	if (bytes < siz) break;
	siz += BUFSIZ;
	mrb_str_resize(mrb_state *mrb, str, siz);
    }
    if (bytes != siz) mrb_str_resize(str, bytes);
    str = io_enc_str(str, fptr);
ENC_CODERANGE_SET(mrb_state *mrb, str, cr);
    return str;
}

void
mrb_io_set_nonblock(mrb_state *mrb, mrb_io_t *fptr)
{
    int oflags;
#ifdef F_GETFL
    oflags = fcntl(fptr->fd, F_GETFL);
    if (oflags == -1) {
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
    }
#else
    oflags = 0;
#endif
    if ((oflags & O_NONBLOCK) == 0) {
        oflags |= O_NONBLOCK;
        if (fcntl(fptr->fd, F_SETFL, oflags) == -1) {
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
        }
    }
}

static mrb_value
io_getpartial(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io, int nonblock)
{
    mrb_io_t *fptr;
    mrb_value length, str;
    long n, len;

mrb_scan_args(mrb_state *mrb, argc, argv, "11", &length, &str);

    if ((len = NUM2LONG(length)) < 0) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "negative length %ld given", len);
    }

io_setstrbuf(mrb_state *mrb, &str,len);
OBJ_TAINT(mrb_state *mrb, str);

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_byte_readable(mrb_state *mrb, fptr);

    if (len == 0)
	return str;

    if (!nonblock)
READ_CHECK(mrb_state *mrb, fptr);
    n = read_buffered_data(RSTRING_PTR(str), len, fptr);
    if (n <= 0) {
      again:
        if (nonblock) {
mrb_io_set_nonblock(mrb_state *mrb, fptr);
        }
	mrb_str_locktmp(mrb_state *mrb, str);
	n = mrb_read_internal(fptr->fd, RSTRING_PTR(str), len);
	mrb_str_unlocktmp(mrb_state *mrb, str);
        if (n < 0) {
            if (!nonblock && mrb_io_wait_readable(fptr->fd))
                goto again;
            if (nonblock && (errno == EWOULDBLOCK || errno == EAGAIN))
mrb_mod_sys_fail(mrb_state *mrb, mrb_mWaitReadable, "read would block");
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
        }
    }
mrb_str_resize(mrb_state *mrb, str, n);

    if (n == 0)
        return Qnil;
    else
        return str;
}

/*
 *  call-seq:
 *     ios.readpartial(maxlen)              -> string
 *     ios.readpartial(maxlen, outbuf)      -> outbuf
 *
 *  Reads at most <i>maxlen</i> bytes from the I/O stream.
 *  It blocks only if <em>ios</em> has no data immediately available.
 *  It doesn't block if some data available.
 *  If the optional <i>outbuf</i> argument is present,
 *  it must reference a String, which will receive the data.
 *  It raises <code>EOFError</code> on end of file.
 *
 *  readpartial is designed for streams such as pipe, socket, tty, etc.
 *  It blocks only when no data immediately available.
 *  This means that it blocks only when following all conditions hold.
 *  * the byte buffer in the IO object is empty.
 *  * the content of the stream is empty.
 *  * the stream is not reached to EOF.
 *
 *  When readpartial blocks, it waits data or EOF on the stream.
 *  If some data is reached, readpartial returns with the data.
 *  If EOF is reached, readpartial raises EOFError.
 *
 *  When readpartial doesn't blocks, it returns or raises immediately.
 *  If the byte buffer is not empty, it returns the data in the buffer.
 *  Otherwise if the stream has some content,
 *  it returns the data in the stream.
 *  Otherwise if the stream is reached to EOF, it raises EOFError.
 *
 *     r, w = IO.pipe           #               buffer          pipe content
 *     w << "abc"               #               ""              "abc".
 *     r.readpartial(4096)      #=> "abc"       ""              ""
 *     r.readpartial(4096)      # blocks because buffer and pipe is empty.
 *
 *     r, w = IO.pipe           #               buffer          pipe content
 *     w << "abc"               #               ""              "abc"
 *     w.close                  #               ""              "abc" EOF
 *     r.readpartial(4096)      #=> "abc"       ""              EOF
 *     r.readpartial(4096)      # raises EOFError
 *
 *     r, w = IO.pipe           #               buffer          pipe content
 *     w << "abc\ndef\n"        #               ""              "abc\ndef\n"
 *     r.gets                   #=> "abc\n"     "def\n"         ""
 *     w << "ghi\n"             #               "def\n"         "ghi\n"
 *     r.readpartial(4096)      #=> "def\n"     ""              "ghi\n"
 *     r.readpartial(4096)      #=> "ghi\n"     ""              ""
 *
 *  Note that readpartial behaves similar to sysread.
 *  The differences are:
 *  * If the byte buffer is not empty, read from the byte buffer instead of "sysread for buffered IO (IOError)".
 *  * It doesn't cause Errno::EWOULDBLOCK and Errno::EINTR.  When readpartial meets EWOULDBLOCK and EINTR by read system call, readpartial retry the system call.
 *
 *  The later means that readpartial is nonblocking-flag insensitive.
 *  It blocks on the situation IO#sysread causes Errno::EWOULDBLOCK as if the fd is blocking mode.
 *
 */

static mrb_value
io_readpartial(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value ret;

    ret = io_getpartial(argc, argv, io, 0);
    if (NIL_P(ret))
mrb_eof_error(mrb_state *mrb, );
    else
        return ret;
}

/*
 *  call-seq:
 *     ios.read_nonblock(maxlen)              -> string
 *     ios.read_nonblock(maxlen, outbuf)      -> outbuf
 *
 *  Reads at most <i>maxlen</i> bytes from <em>ios</em> using
 *  the read(2) system call after O_NONBLOCK is set for
 *  the underlying file descriptor.
 *
 *  If the optional <i>outbuf</i> argument is present,
 *  it must reference a String, which will receive the data.
 *
 *  read_nonblock just calls the read(2) system call.
 *  It causes all errors the read(2) system call causes: Errno::EWOULDBLOCK, Errno::EINTR, etc.
 *  The caller should care such errors.
 *
 *  If the exception is Errno::EWOULDBLOCK or Errno::AGAIN,
 *  it is extended by IO::WaitReadable.
 *  So IO::WaitReadable can be used to rescue the exceptions for retrying read_nonblock.
 *
 *  read_nonblock causes EOFError on EOF.
 *
 *  If the read byte buffer is not empty,
 *  read_nonblock reads from the buffer like readpartial.
 *  In this case, the read(2) system call is not called.
 *
 *  When read_nonblock raises an exception kind of IO::WaitReadable,
 *  read_nonblock should not be called
 *  until io is readable for avoiding busy loop.
 *  This can be done as follows.
 *
 *    # emulates blocking read (readpartial).
 *    begin
 *      result = io.read_nonblock(maxlen)
 *    rescue IO::WaitReadable
 *      IO.select([io])
 *      retry
 *    end
 *
 *  Although IO#read_nonblock doesn't raise IO::WaitWritable.
 *  OpenSSL::Buffering#read_nonblock can raise IO::WaitWritable.
 *  If IO and SSL should be used polymorphically,
 *  IO::WaitWritable should be rescued too.
 *  See the document of OpenSSL::Buffering#read_nonblock for sample code.
 *
 *  Note that this method is identical to readpartial
 *  except the non-blocking flag is set.
 */

static mrb_value
io_read_nonblock(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value ret;

    ret = io_getpartial(argc, argv, io, 1);
    if (NIL_P(ret))
mrb_eof_error(mrb_state *mrb, );
    else
        return ret;
}

/*
 *  call-seq:
 *     ios.write_nonblock(string)   -> integer
 *
 *  Writes the given string to <em>ios</em> using
 *  the write(2) system call after O_NONBLOCK is set for
 *  the underlying file descriptor.
 *
 *  It returns the number of bytes written.
 *
 *  write_nonblock just calls the write(2) system call.
 *  It causes all errors the write(2) system call causes: Errno::EWOULDBLOCK, Errno::EINTR, etc.
 *  The result may also be smaller than string.length (partial write).
 *  The caller should care such errors and partial write.
 *
 *  If the exception is Errno::EWOULDBLOCK or Errno::AGAIN,
 *  it is extended by IO::WaitWritable.
 *  So IO::WaitWritable can be used to rescue the exceptions for retrying write_nonblock.
 *
 *    # Creates a pipe.
 *    r, w = IO.pipe
 *
 *    # write_nonblock writes only 65536 bytes and return 65536.
 *    # (The pipe size is 65536 bytes on this environment.)
 *    s = "a" * 100000
 *    p w.write_nonblock(s)     #=> 65536
 *
 *    # write_nonblock cannot write a byte and raise EWOULDBLOCK (EAGAIN).
 *    p w.write_nonblock("b")   # Resource temporarily unavailable (Errno::EAGAIN)
 *
 *  If the write buffer is not empty, it is flushed at first.
 *
 *  When write_nonblock raises an exception kind of IO::WaitWritable,
 *  write_nonblock should not be called
 *  until io is writable for avoiding busy loop.
 *  This can be done as follows.
 *
 *    begin
 *      result = io.write_nonblock(string)
 *    rescue IO::WaitWritable, Errno::EINTR
 *      IO.select(nil, [io])
 *      retry
 *    end
 *
 *  Note that this doesn't guarantee to write all data in string.
 *  The length written is reported as result and it should be checked later.
 *
 *  On some platforms such as Windows, write_nonblock is not supported
 *  according to the kind of the IO object.
 *  In such cases, write_nonblock raises <code>Errno::EBADF</code>.
 *
 */

static mrb_value
mrb_io_write_nonblock(mrb_state *mrb, mrb_value io, mrb_value str)
{
    mrb_io_t *fptr;
    long n;

mrb_secure(mrb_state *mrb, 4);
    if (TYPE(str) != T_STRING)
	str = mrb_obj_as_string(str);

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_writable(mrb_state *mrb, fptr);

    if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);

mrb_io_set_nonblock(mrb_state *mrb, fptr);
    n = write(fptr->fd, RSTRING_PTR(str), RSTRING_LEN(str));

    if (n == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
mrb_mod_sys_fail(mrb_state *mrb, mrb_mWaitWritable, "write would block");
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
    }

    return LONG2FIX(n);
}

/*
 *  call-seq:
 *     ios.read([length [, buffer]])    -> string, buffer, or nil
 *
 *  Reads <i>length</i> bytes from the I/O stream.
 *
 *  <i>length</i> must be a non-negative integer or <code>nil</code>.
 *
 *  If <i>length</i> is a positive integer,
 *  it try to read <i>length</i> bytes without any conversion (binary mode).
 *  It returns <code>nil</code> or a string whose length is 1 to <i>length</i> bytes.
 *  <code>nil</code> means it met EOF at beginning.
 *  The 1 to <i>length</i>-1 bytes string means it met EOF after reading the result.
 *  The <i>length</i> bytes string means it doesn't meet EOF.
 *  The resulted string is always ASCII-8BIT encoding.
 *
 *  If <i>length</i> is omitted or is <code>nil</code>,
 *  it reads until EOF and the encoding conversion is applied.
 *  It returns a string even if EOF is met at beginning.
 *
 *  If <i>length</i> is zero, it returns <code>""</code>.
 *
 *  If the optional <i>buffer</i> argument is present, it must reference
 *  a String, which will receive the data.
 *
 *  At end of file, it returns <code>nil</code> or <code>""</code>
 *  depend on <i>length</i>.
 *  <code><i>ios</i>.read()</code> and
 *  <code><i>ios</i>.read(nil)</code> returns <code>""</code>.
 *  <code><i>ios</i>.read(<i>positive-integer</i>)</code> returns <code>nil</code>.
 *
 *     f = File.new("testfile")
 *     f.read(16)   #=> "This is line one"
 *
 *     # reads whole file
 *     open("file") {|f|
 *       data = f.read # This returns a string even if the file is empty.
 *       ...
 *     }
 *
 *     # iterate over fixed length records.
 *     open("fixed-record-file") {|f|
 *       while record = f.read(256)
 *         ...
 *       end
 *     }
 *
 *     # iterate over variable length records.
 *     # record is prefixed by 32-bit length.
 *     open("variable-record-file") {|f|
 *       while len = f.read(4)
 *         len = len.unpack("N")[0] # 32-bit length
 *         record = f.read(len) # This returns a string even if len is 0.
 *       end
 *     }
 *
 *  Note that this method behaves like fread() function in C.
 *  If you need the behavior like read(2) system call,
 *  consider readpartial, read_nonblock and sysread.
 */

static mrb_value
io_read(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_io_t *fptr;
    long n, len;
    mrb_value length, str;
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
    int previous_mode;
#endif

mrb_scan_args(mrb_state *mrb, argc, argv, "02", &length, &str);

    if (NIL_P(length)) {
	GetOpenFile(mrb_state *mrb, io, fptr);
	mrb_io_check_char_readable(mrb_state *mrb, fptr);
	return read_all(fptr, remain_size(fptr), str);
    }
    len = NUM2LONG(length);
    if (len < 0) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "negative length %ld given", len);
    }

io_setstrbuf(mrb_state *mrb, &str,len);

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_byte_readable(mrb_state *mrb, fptr);
    if (len == 0) return str;

READ_CHECK(mrb_state *mrb, fptr);
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
    previous_mode = set_binary_mode_with_seek_cur(fptr);
#endif
    n = io_fread(str, 0, fptr);
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
    if (previous_mode == O_TEXT) {
	setmode(mrb_state *mrb, fptr->fd, O_TEXT);
    }
#endif
    if (n == 0) {
	if (fptr->fd < 0) return Qnil;
mrb_str_resize(mrb_state *mrb, str, 0);
        return Qnil;
    }
mrb_str_resize(mrb_state *mrb, str, n);
OBJ_TAINT(mrb_state *mrb, str);

    return str;
}

static void
rscheck(mrb_state *mrb, const char *rsptr, long rslen, mrb_value rs)
{
    if (!rs) return;
    if (RSTRING_PTR(rs) != rsptr && RSTRING_LEN(rs) != rslen)
	mrb_raise(mrb_state *mrb, mrb_eRuntimeError, "rs modified");
}

static int
appendline(mrb_state *mrb, mrb_io_t *fptr, int delim, mrb_value *strp, long *lp)
{
    mrb_value str = *strp;
    long limit = *lp;

    if (NEED_READCONV(fptr)) {
	SET_BINARY_MODE(mrb_state *mrb, fptr);
make_readconv(mrb_state *mrb, fptr, 0);
        do {
            const char *p, *e;
            int searchlen;
            if (fptr->cbuf.len) {
                p = fptr->cbuf.ptr+fptr->cbuf.off;
                searchlen = fptr->cbuf.len;
                if (0 < limit && limit < searchlen)
                    searchlen = (int)limit;
                e = memchr(p, delim, searchlen);
                if (e) {
		    int len = (int)(e-p+1);
                    if (NIL_P(str))
                        *strp = str = mrb_str_new(p, len);
                    else
mrb_str_buf_cat(mrb_state *mrb, str, p, len);
                    fptr->cbuf.off += len;
                    fptr->cbuf.len -= len;
                    limit -= len;
                    *lp = limit;
                    return delim;
                }

                if (NIL_P(str))
                    *strp = str = mrb_str_new(p, searchlen);
                else
mrb_str_buf_cat(mrb_state *mrb, str, p, searchlen);
                fptr->cbuf.off += searchlen;
                fptr->cbuf.len -= searchlen;
                limit -= searchlen;

                if (limit == 0) {
                    *lp = limit;
                    return (unsigned char)RSTRING_PTR(str)[RSTRING_LEN(str)-1];
                }
            }
        } while (more_char(fptr) != MORE_CHAR_FINISHED);
clear_readconv(mrb_state *mrb, fptr);
        *lp = limit;
        return EOF;
    }

NEED_NEWLINE_DECORATOR_ON_READ_CHECK(mrb_state *mrb, fptr);
    do {
	long pending = READ_DATA_PENDING_COUNT(fptr);
	if (pending > 0) {
	    const char *p = READ_DATA_PENDING_PTR(fptr);
	    const char *e;
	    long last;

	    if (limit > 0 && pending > limit) pending = limit;
	    e = memchr(p, delim, pending);
	    if (e) pending = e - p + 1;
	    if (!NIL_P(str)) {
		last = RSTRING_LEN(str);
		mrb_str_resize(mrb_state *mrb, str, last + pending);
	    }
	    else {
                last = 0;
		*strp = str = mrb_str_buf_new(pending);
		mrb_str_set_len(mrb_state *mrb, str, pending);
	    }
	    read_buffered_data(RSTRING_PTR(str) + last, pending, fptr); /* must not fail */
	    limit -= pending;
	    *lp = limit;
	    if (e) return delim;
	    if (limit == 0)
		return (unsigned char)RSTRING_PTR(str)[RSTRING_LEN(str)-1];
	}
	READ_CHECK(mrb_state *mrb, fptr);
    } while (io_fillbuf(fptr) >= 0);
    *lp = limit;
    return EOF;
}

static inline int
swallow(mrb_state *mrb, mrb_io_t *fptr, int term)
{
    if (NEED_READCONV(fptr)) {
	mrb_encoding *enc = io_read_encoding(fptr);
	int needconv = mrb_enc_mbminlen(enc) != 1;
	SET_BINARY_MODE(mrb_state *mrb, fptr);
	make_readconv(mrb_state *mrb, fptr, 0);
	do {
	    size_t cnt;
	    while ((cnt = READ_CHAR_PENDING_COUNT(fptr)) > 0) {
		const char *p = READ_CHAR_PENDING_PTR(fptr);
		int i;
		if (!needconv) {
		    if (*p != term) return TRUE;
		    i = (int)cnt;
		    while (--i && *++p == term);
		}
		else {
		    const char *e = p + cnt;
		    if (mrb_enc_ascget(p, e, &i, enc) != term) return TRUE;
		    while ((p += i) < e && mrb_enc_ascget(p, e, &i, enc) == term);
		    i = (int)(e - p);
		}
		io_shift_cbuf(mrb_state *mrb, fptr, (int)cnt - i, NULL);
	    }
	} while (more_char(fptr) != MORE_CHAR_FINISHED);
	return FALSE;
    }

NEED_NEWLINE_DECORATOR_ON_READ_CHECK(mrb_state *mrb, fptr);
    do {
	size_t cnt;
	while ((cnt = READ_DATA_PENDING_COUNT(fptr)) > 0) {
	    char buf[1024];
	    const char *p = READ_DATA_PENDING_PTR(fptr);
	    int i;
	    if (cnt > sizeof buf) cnt = sizeof buf;
	    if (*p != term) return TRUE;
	    i = (int)cnt;
	    while (--i && *++p == term);
	    if (!read_buffered_data(buf, cnt - i, fptr)) /* must not fail */
		mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
	}
	READ_CHECK(mrb_state *mrb, fptr);
    } while (io_fillbuf(fptr) == 0);
    return FALSE;
}

static mrb_value
mrb_io_getline_fast(mrb_state *mrb, mrb_io_t *fptr, mrb_encoding *enc, mrb_value io)
{
    mrb_value str = Qnil;
    int len = 0;
    long pos = 0;
    int cr = 0;

    for (;;) {
	int pending = READ_DATA_PENDING_COUNT(fptr);

	if (pending > 0) {
	    const char *p = READ_DATA_PENDING_PTR(fptr);
	    const char *e;

	    e = memchr(p, '\n', pending);
	    if (e) {
                pending = (int)(e - p + 1);
	    }
	    if (NIL_P(str)) {
		str = mrb_str_new(p, pending);
		fptr->rbuf.off += pending;
		fptr->rbuf.len -= pending;
	    }
	    else {
		mrb_str_resize(mrb_state *mrb, str, len + pending);
		read_buffered_data(mrb_state *mrb, RSTRING_PTR(str)+len, pending, fptr);
	    }
	    len += pending;
	    if (cr != ENC_CODERANGE_BROKEN)
		pos += mrb_str_coderange_scan_restartable(RSTRING_PTR(str) + pos, RSTRING_PTR(str) + len, enc, &cr);
	    if (e) break;
	}
	READ_CHECK(mrb_state *mrb, fptr);
	if (io_fillbuf(fptr) < 0) {
	    if (NIL_P(str)) return Qnil;
	    break;
	}
    }

    str = io_enc_str(str, fptr);
ENC_CODERANGE_SET(mrb_state *mrb, str, cr);
    fptr->lineno++;
    if (io == ARGF.current_file) {
	ARGF.lineno++;
	ARGF.last_lineno = ARGF.lineno;
    }
    else {
	ARGF.last_lineno = fptr->lineno;
    }

    return str;
}

static void
prepare_getline_args(mrb_state *mrb, int argc, mrb_value *argv, mrb_value *rsp, long *limit, mrb_value io)
{
    mrb_value rs = mrb_rs, lim = Qnil;
    mrb_io_t *fptr;

    if (argc == 1) {
        mrb_value tmp = Qnil;

        if (NIL_P(argv[0]) || !NIL_P(tmp = mrb_check_string_type(argv[0]))) {
            rs = tmp;
        }
        else {
            lim = argv[0];
        }
    }
    else if (2 <= argc) {
mrb_scan_args(mrb_state *mrb, argc, argv, "2", &rs, &lim);
        if (!NIL_P(rs))
StringValue(mrb_state *mrb, rs);
    }
    if (!NIL_P(rs)) {
	mrb_encoding *enc_rs, *enc_io;

	GetOpenFile(mrb_state *mrb, io, fptr);
	enc_rs = mrb_enc_get(rs);
	enc_io = io_read_encoding(fptr);
	if (enc_io != enc_rs &&
	    (mrb_enc_str_coderange(rs) != ENC_CODERANGE_7BIT ||
	     (RSTRING_LEN(rs) > 0 && !mrb_enc_asciicompat(enc_io)))) {
            if (rs == mrb_default_rs) {
                rs = mrb_enc_str_new(0, 0, enc_io);
mrb_str_buf_cat_ascii(mrb_state *mrb, rs, "\n");
            }
            else {
mrb_raise(mrb_state *mrb, mrb_eArgError, "encoding mismatch: %s IO with %s RS",
mrb_enc_name(mrb_state *mrb, enc_io),
mrb_enc_name(mrb_state *mrb, enc_rs));
            }
	}
    }
    *rsp = rs;
    *limit = NIL_P(lim) ? -1L : NUM2LONG(lim);
}

static mrb_value
mrb_io_getline_1(mrb_state *mrb, mrb_value rs, long limit, mrb_value io)
{
    mrb_value str = Qnil;
    mrb_io_t *fptr;
    int nolimit = 0;
    mrb_encoding *enc;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);
    if (NIL_P(rs) && limit < 0) {
	str = read_all(fptr, 0, Qnil);
	if (RSTRING_LEN(str) == 0) return Qnil;
    }
    else if (limit == 0) {
	return mrb_enc_str_new(0, 0, io_read_encoding(fptr));
    }
    else if (rs == mrb_default_rs && limit < 0 && !NEED_READCONV(fptr) &&
mrb_enc_asciicompat(mrb_state *mrb, enc = io_read_encoding(fptr))) {
	NEED_NEWLINE_DECORATOR_ON_READ_CHECK(mrb_state *mrb, fptr);
	return mrb_io_getline_fast(fptr, enc, io);
    }
    else {
	int c, newline = -1;
	const char *rsptr = 0;
	long rslen = 0;
	int rspara = 0;
        int extra_limit = 16;

	SET_BINARY_MODE(mrb_state *mrb, fptr);
        enc = io_read_encoding(fptr);

	if (!NIL_P(rs)) {
	    rslen = RSTRING_LEN(rs);
	    if (rslen == 0) {
		rsptr = "\n\n";
		rslen = 2;
		rspara = 1;
		swallow(mrb_state *mrb, fptr, '\n');
		rs = 0;
		if (!mrb_enc_asciicompat(enc)) {
		    rs = mrb_usascii_str_new(rsptr, rslen);
		    rs = mrb_str_encode(rs, mrb_enc_from_encoding(enc), 0, Qnil);
		    OBJ_FREEZE(rs);
		    rsptr = RSTRING_PTR(rs);
		    rslen = RSTRING_LEN(rs);
		}
	    }
	    else {
		rsptr = RSTRING_PTR(rs);
	    }
	    newline = (unsigned char)rsptr[rslen - 1];
	}

	/* MS - Optimisation */
	while ((c = appendline(fptr, newline, &str, &limit)) != EOF) {
            const char *s, *p, *pp, *e;

	    if (c == newline) {
		if (RSTRING_LEN(str) < rslen) continue;
		s = RSTRING_PTR(str);
                e = s + RSTRING_LEN(str);
		p = e - rslen;
		pp = mrb_enc_left_char_head(s, p, e, enc);
		if (pp != p) continue;
		if (!rspara) rscheck(rsptr, rslen, rs);
		if (memcmp(p, rsptr, rslen) == 0) break;
	    }
	    if (limit == 0) {
		s = RSTRING_PTR(str);
		p = s + RSTRING_LEN(str);
		pp = mrb_enc_left_char_head(s, p-1, p, enc);
                if (extra_limit &&
MBCLEN_NEEDMORE_P(mrb_state *mrb, mrb_enc_precise_mbclen(pp, p, enc))) {
                    /* relax the limit while incomplete character.
                     * extra_limit limits the relax length */
                    limit = 1;
                    extra_limit--;
                }
                else {
                    nolimit = 1;
                    break;
                }
	    }
	}

	if (rspara) {
	    if (c != EOF) {
		swallow(mrb_state *mrb, fptr, '\n');
	    }
	}
	if (!NIL_P(str))
            str = io_enc_str(str, fptr);
    }

    if (!NIL_P(str)) {
	if (!nolimit) {
	    fptr->lineno++;
	    if (io == ARGF.current_file) {
		ARGF.lineno++;
		ARGF.last_lineno = ARGF.lineno;
	    }
	    else {
		ARGF.last_lineno = fptr->lineno;
	    }
	}
    }

    return str;
}

static mrb_value
mrb_io_getline(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value rs;
    long limit;

prepare_getline_args(mrb_state *mrb, argc, argv, &rs, &limit, io);
    return mrb_io_getline_1(rs, limit, io);
}

mrb_value
mrb_io_gets(mrb_state *mrb, mrb_value io)
{
    return mrb_io_getline_1(mrb_default_rs, -1, io);
}

/*
 *  call-seq:
 *     ios.gets(sep=$/)     -> string or nil
 *     ios.gets(limit)      -> string or nil
 *     ios.gets(sep, limit) -> string or nil
 *
 *  Reads the next ``line'' from the I/O stream; lines are separated by
 *  <i>sep</i>. A separator of <code>nil</code> reads the entire
 *  contents, and a zero-length separator reads the input a paragraph at
 *  a time (two successive newlines in the input separate paragraphs).
 *  The stream must be opened for reading or an <code>IOError</code>
 *  will be raised. The line read in will be returned and also assigned
 *  to <code>$_</code>. Returns <code>nil</code> if called at end of
 *  file.  If the first argument is an integer, or optional second
 *  argument is given, the returning string would not be longer than the
 *  given value in bytes.
 *
 *     File.new("testfile").gets   #=> "This is line one\n"
 *     $_                          #=> "This is line one\n"
 */

static mrb_value
mrb_io_gets_m(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value str;

    str = mrb_io_getline(argc, argv, io);
mrb_lastline_set(mrb_state *mrb, str);

    return str;
}

/*
 *  call-seq:
 *     ios.lineno    -> integer
 *
 *  Returns the current line number in <em>ios</em>.  The stream must be
 *  opened for reading. <code>lineno</code> counts the number of times
 *  #gets is called rather than the number of newlines encountered.  The two
 *  values will differ if #gets is called with a separator other than newline.
 *
 *  Methods that use <code>$/</code> like #each, #lines and #readline will
 *  also increment <code>lineno</code>.
 *
 *  See also the <code>$.</code> variable.
 *
 *     f = File.new("testfile")
 *     f.lineno   #=> 0
 *     f.gets     #=> "This is line one\n"
 *     f.lineno   #=> 1
 *     f.gets     #=> "This is line two\n"
 *     f.lineno   #=> 2
 */

static mrb_value
mrb_io_lineno(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);
    return INT2NUM(fptr->lineno);
}

/*
 *  call-seq:
 *     ios.lineno = integer    -> integer
 *
 *  Manually sets the current line number to the given value.
 *  <code>$.</code> is updated only on the next read.
 *
 *     f = File.new("testfile")
 *     f.gets                     #=> "This is line one\n"
 *     $.                         #=> 1
 *     f.lineno = 1000
 *     f.lineno                   #=> 1000
 *     $.                         #=> 1         # lineno of last read
 *     f.gets                     #=> "This is line two\n"
 *     $.                         #=> 1001      # lineno of last read
 */

static mrb_value
mrb_io_set_lineno(mrb_state *mrb, mrb_value io, mrb_value lineno)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);
    fptr->lineno = NUM2INT(lineno);
    return lineno;
}

/*
 *  call-seq:
 *     ios.readline(sep=$/)     -> string
 *     ios.readline(limit)      -> string
 *     ios.readline(sep, limit) -> string
 *
 *  Reads a line as with <code>IO#gets</code>, but raises an
 *  <code>EOFError</code> on end of file.
 */

static mrb_value
mrb_io_readline(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value line = mrb_io_gets_m(argc, argv, io);

    if (NIL_P(line)) {
	mrb_eof_error(mrb_state *mrb, );
    }
    return line;
}

/*
 *  call-seq:
 *     ios.readlines(sep=$/)     -> array
 *     ios.readlines(limit)      -> array
 *     ios.readlines(sep, limit) -> array
 *
 *  Reads all of the lines in <em>ios</em>, and returns them in
 *  <i>anArray</i>. Lines are separated by the optional <i>sep</i>. If
 *  <i>sep</i> is <code>nil</code>, the rest of the stream is returned
 *  as a single record.  If the first argument is an integer, or
 *  optional second argument is given, the returning string would not be
 *  longer than the given value in bytes. The stream must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *     f = File.new("testfile")
 *     f.readlines[0]   #=> "This is line one\n"
 */

static mrb_value
mrb_io_readlines(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value line, ary, rs;
    long limit;

prepare_getline_args(mrb_state *mrb, argc, argv, &rs, &limit, io);
    if (limit == 0)
	mrb_raise(mrb_state *mrb, mrb_eArgError, "invalid limit: 0 for readlines");
    ary = mrb_ary_new();
    while (!NIL_P(line = mrb_io_getline_1(rs, limit, io))) {
	mrb_ary_push(mrb_state *mrb, ary, line);
    }
    return ary;
}

/*
 *  call-seq:
 *     ios.each(sep=$/) {|line| block }         -> ios
 *     ios.each(limit) {|line| block }          -> ios
 *     ios.each(sep,limit) {|line| block }      -> ios
 *     ios.each(...)                            -> an_enumerator
 *
 *     ios.each_line(sep=$/) {|line| block }    -> ios
 *     ios.each_line(limit) {|line| block }     -> ios
 *     ios.each_line(sep,limit) {|line| block } -> ios
 *     ios.each_line(...)                       -> an_enumerator
 *
 *     ios.lines(sep=$/) {|line| block }        -> ios
 *     ios.lines(limit) {|line| block }         -> ios
 *     ios.lines(sep,limit) {|line| block }     -> ios
 *     ios.lines(...)                           -> an_enumerator
 *
 *  Executes the block for every line in <em>ios</em>, where lines are
 *  separated by <i>sep</i>. <em>ios</em> must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     f = File.new("testfile")
 *     f.each {|line| puts "#{f.lineno}: #{line}" }
 *
 *  <em>produces:</em>
 *
 *     1: This is line one
 *     2: This is line two
 *     3: This is line three
 *     4: And so on...
 */

static mrb_value
mrb_io_each_line(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value str, rs;
    long limit;

RETURN_ENUMERATOR(mrb_state *mrb, io, argc, argv);
prepare_getline_args(mrb_state *mrb, argc, argv, &rs, &limit, io);
    if (limit == 0)
	mrb_raise(mrb_state *mrb, mrb_eArgError, "invalid limit: 0 for each_line");
    while (!NIL_P(str = mrb_io_getline_1(rs, limit, io))) {
	mrb_yield(mrb_state *mrb, str);
    }
    return io;
}

/*
 *  call-seq:
 *     ios.bytes {|byte| block }      -> ios
 *     ios.bytes                      -> an_enumerator
 *
 *     ios.each_byte {|byte| block }  -> ios
 *     ios.each_byte                  -> an_enumerator
 *
 *  Calls the given block once for each byte (0..255) in <em>ios</em>,
 *  passing the byte as an argument. The stream must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     f = File.new("testfile")
 *     checksum = 0
 *     f.each_byte {|x| checksum ^= x }   #=> #<File:testfile>
 *     checksum                           #=> 12
 */

static mrb_value
mrb_io_each_byte(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    char *p, *e;

RETURN_ENUMERATOR(mrb_state *mrb, io, 0, 0);
GetOpenFile(mrb_state *mrb, io, fptr);

    for (;;) {
	while (fptr->rbuf.len > 0) {
	    p = fptr->rbuf.ptr + fptr->rbuf.off++;
	    e = p + fptr->rbuf.len--;
	    mrb_yield(INT2FIX(*p & 0xff));
	    errno = 0;
	}
	mrb_io_check_byte_readable(mrb_state *mrb, fptr);
	READ_CHECK(mrb_state *mrb, fptr);
	if (io_fillbuf(fptr) < 0) {
	    break;
	}
    }
    return io;
}

static mrb_value
io_getc(mrb_state *mrb, mrb_io_t *fptr, mrb_encoding *enc)
{
    int r, n, cr = 0;
    mrb_value str;

    if (NEED_READCONV(fptr)) {
        mrb_value str = Qnil;
	mrb_encoding *read_enc = io_read_encoding(fptr);

	SET_BINARY_MODE(mrb_state *mrb, fptr);
make_readconv(mrb_state *mrb, fptr, 0);

        while (1) {
            if (fptr->cbuf.len) {
		r = mrb_enc_precise_mbclen(fptr->cbuf.ptr+fptr->cbuf.off,
			fptr->cbuf.ptr+fptr->cbuf.off+fptr->cbuf.len,
			read_enc);
                if (!MBCLEN_NEEDMORE_P(r))
                    break;
                if (fptr->cbuf.len == fptr->cbuf.capa) {
mrb_raise(mrb_state *mrb, mrb_eIOError, "too long character");
                }
            }

            if (more_char(fptr) == MORE_CHAR_FINISHED) {
                if (fptr->cbuf.len == 0) {
		    clear_readconv(fptr);
		    return Qnil;
		}
                /* return an unit of an incomplete character just before EOF */
		str = mrb_enc_str_new(fptr->cbuf.ptr+fptr->cbuf.off, 1, read_enc);
		fptr->cbuf.off += 1;
		fptr->cbuf.len -= 1;
                if (fptr->cbuf.len == 0) clear_readconv(fptr);
		ENC_CODERANGE_SET(mrb_state *mrb, str, ENC_CODERANGE_BROKEN);
		return str;
            }
        }
        if (MBCLEN_INVALID_P(r)) {
            r = mrb_enc_mbclen(fptr->cbuf.ptr+fptr->cbuf.off,
                              fptr->cbuf.ptr+fptr->cbuf.off+fptr->cbuf.len,
                              read_enc);
io_shift_cbuf(mrb_state *mrb, fptr, r, &str);
	    cr = ENC_CODERANGE_BROKEN;
	}
	else {
	    io_shift_cbuf(fptr, MBCLEN_CHARFOUND_LEN(r), &str);
	    cr = ISASCII(r) ? ENC_CODERANGE_7BIT : ENC_CODERANGE_VALID;
	}
	str = io_enc_str(str, fptr);
	ENC_CODERANGE_SET(mrb_state *mrb, str, cr);
	return str;
    }

NEED_NEWLINE_DECORATOR_ON_READ_CHECK(mrb_state *mrb, fptr);
    if (io_fillbuf(fptr) < 0) {
	return Qnil;
    }
    if (mrb_enc_asciicompat(enc) && ISASCII(fptr->rbuf.ptr[fptr->rbuf.off])) {
	str = mrb_str_new(fptr->rbuf.ptr+fptr->rbuf.off, 1);
	fptr->rbuf.off += 1;
	fptr->rbuf.len -= 1;
	cr = ENC_CODERANGE_7BIT;
    }
    else {
	r = mrb_enc_precise_mbclen(fptr->rbuf.ptr+fptr->rbuf.off, fptr->rbuf.ptr+fptr->rbuf.off+fptr->rbuf.len, enc);
	if (MBCLEN_CHARFOUND_P(r) &&
	    (n = MBCLEN_CHARFOUND_LEN(r)) <= fptr->rbuf.len) {
	    str = mrb_str_new(fptr->rbuf.ptr+fptr->rbuf.off, n);
	    fptr->rbuf.off += n;
	    fptr->rbuf.len -= n;
	    cr = ENC_CODERANGE_VALID;
	}
	else if (MBCLEN_NEEDMORE_P(r)) {
	    str = mrb_str_new(fptr->rbuf.ptr+fptr->rbuf.off, fptr->rbuf.len);
	    fptr->rbuf.len = 0;
	  getc_needmore:
	    if (io_fillbuf(fptr) != -1) {
		mrb_str_cat(mrb_state *mrb, str, fptr->rbuf.ptr+fptr->rbuf.off, 1);
		fptr->rbuf.off++;
		fptr->rbuf.len--;
		r = mrb_enc_precise_mbclen(RSTRING_PTR(str), RSTRING_PTR(str)+RSTRING_LEN(str), enc);
		if (MBCLEN_NEEDMORE_P(r)) {
		    goto getc_needmore;
		}
		else if (MBCLEN_CHARFOUND_P(r)) {
		    cr = ENC_CODERANGE_VALID;
		}
	    }
	}
	else {
	    str = mrb_str_new(fptr->rbuf.ptr+fptr->rbuf.off, 1);
	    fptr->rbuf.off++;
	    fptr->rbuf.len--;
	}
    }
    if (!cr) cr = ENC_CODERANGE_BROKEN;
    str = io_enc_str(str, fptr);
ENC_CODERANGE_SET(mrb_state *mrb, str, cr);
    return str;
}

/*
 *  call-seq:
 *     ios.chars {|c| block }      -> ios
 *     ios.chars                   -> an_enumerator
 *
 *     ios.each_char {|c| block }  -> ios
 *     ios.each_char               -> an_enumerator
 *
 *  Calls the given block once for each character in <em>ios</em>,
 *  passing the character as an argument. The stream must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     f = File.new("testfile")
 *     f.each_char {|c| print c, ' ' }   #=> #<File:testfile>
 */

static mrb_value
mrb_io_each_char(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_encoding *enc;
    mrb_value c;

RETURN_ENUMERATOR(mrb_state *mrb, io, 0, 0);
GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);

    enc = io_input_encoding(fptr);
READ_CHECK(mrb_state *mrb, fptr);
    while (!NIL_P(c = io_getc(fptr, enc))) {
mrb_yield(mrb_state *mrb, c);
    }
    return io;
}


/*
 *  call-seq:
 *     ios.each_codepoint {|c| block }  -> ios
 *     ios.codepoints     {|c| block }  -> ios
 *     ios.each_codepoint               -> an_enumerator
 *     ios.codepoints                   -> an_enumerator
 *
 *  Passes the <code>Integer</code> ordinal of each character in <i>ios</i>,
 *  passing the codepoint as an argument. The stream must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 */

static mrb_value
mrb_io_each_codepoint(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_encoding *enc;
    unsigned int c;
    int r, n;

RETURN_ENUMERATOR(mrb_state *mrb, io, 0, 0);
GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);

READ_CHECK(mrb_state *mrb, fptr);
    if (NEED_READCONV(fptr)) {
	SET_BINARY_MODE(mrb_state *mrb, fptr);
	for (;;) {
	    make_readconv(fptr, 0);
	    for (;;) {
		if (fptr->cbuf.len) {
		    if (fptr->encs.enc)
			r = mrb_enc_precise_mbclen(fptr->cbuf.ptr+fptr->cbuf.off,
						  fptr->cbuf.ptr+fptr->cbuf.off+fptr->cbuf.len,
						  fptr->encs.enc);
		    else
			r = ONIGENC_CONSTRUCT_MBCLEN_CHARFOUND(1);
		    if (!MBCLEN_NEEDMORE_P(r))
			break;
		    if (fptr->cbuf.len == fptr->cbuf.capa) {
			mrb_raise(mrb_state *mrb, mrb_eIOError, "too long character");
		    }
		}
		if (more_char(fptr) == MORE_CHAR_FINISHED) {
clear_readconv(mrb_state *mrb, fptr);
		    /* ignore an incomplete character before EOF */
		    return io;
		}
	    }
	    if (MBCLEN_INVALID_P(r)) {
		mrb_raise(mrb_state *mrb, mrb_eArgError, "invalid byte sequence in %s",
			 mrb_enc_name(fptr->encs.enc));
	    }
	    n = MBCLEN_CHARFOUND_LEN(r);
	    if (fptr->encs.enc) {
		c = mrb_enc_codepoint(fptr->cbuf.ptr+fptr->cbuf.off,
				     fptr->cbuf.ptr+fptr->cbuf.off+fptr->cbuf.len,
				     fptr->encs.enc);
	    }
	    else {
		c = (unsigned char)fptr->cbuf.ptr[fptr->cbuf.off];
	    }
	    fptr->cbuf.off += n;
	    fptr->cbuf.len -= n;
	    mrb_yield(UINT2NUM(c));
	}
    }
NEED_NEWLINE_DECORATOR_ON_READ_CHECK(mrb_state *mrb, fptr);
    enc = io_input_encoding(fptr);
    for (;;) {
	if (io_fillbuf(fptr) < 0) {
	    return io;
	}
	r = mrb_enc_precise_mbclen(fptr->rbuf.ptr+fptr->rbuf.off,
				  fptr->rbuf.ptr+fptr->rbuf.off+fptr->rbuf.len, enc);
	if (MBCLEN_CHARFOUND_P(r) &&
	    (n = MBCLEN_CHARFOUND_LEN(r)) <= fptr->rbuf.len) {
	    c = mrb_enc_codepoint(fptr->rbuf.ptr+fptr->rbuf.off,
				 fptr->rbuf.ptr+fptr->rbuf.off+fptr->rbuf.len, enc);
	    fptr->rbuf.off += n;
	    fptr->rbuf.len -= n;
	    mrb_yield(UINT2NUM(c));
	}
	else if (MBCLEN_INVALID_P(r)) {
	    mrb_raise(mrb_eArgError, "invalid byte sequence in %s", mrb_enc_name(enc));
	}
	else {
	    continue;
	}
    }
    return io;
}



/*
 *  call-seq:
 *     ios.getc   -> string or nil
 *
 *  Reads a one-character string from <em>ios</em>. Returns
 *  <code>nil</code> if called at end of file.
 *
 *     f = File.new("testfile")
 *     f.getc   #=> "h"
 *     f.getc   #=> "e"
 */

static mrb_value
mrb_io_getc(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_encoding *enc;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);

    enc = io_input_encoding(fptr);
READ_CHECK(mrb_state *mrb, fptr);
    return io_getc(fptr, enc);
}

/*
 *  call-seq:
 *     ios.readchar   -> string
 *
 *  Reads a one-character string from <em>ios</em>. Raises an
 *  <code>EOFError</code> on end of file.
 *
 *     f = File.new("testfile")
 *     f.readchar   #=> "h"
 *     f.readchar   #=> "e"
 */

static mrb_value
mrb_io_readchar(mrb_state *mrb, mrb_value io)
{
    mrb_value c = mrb_io_getc(io);

    if (NIL_P(c)) {
	mrb_eof_error(mrb_state *mrb, );
    }
    return c;
}

/*
 *  call-seq:
 *     ios.getbyte   -> fixnum or nil
 *
 *  Gets the next 8-bit byte (0..255) from <em>ios</em>. Returns
 *  <code>nil</code> if called at end of file.
 *
 *     f = File.new("testfile")
 *     f.getbyte   #=> 84
 *     f.getbyte   #=> 104
 */

mrb_value
mrb_io_getbyte(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    int c;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_byte_readable(mrb_state *mrb, fptr);
READ_CHECK(mrb_state *mrb, fptr);
    if (fptr->fd == 0 && (fptr->mode & FMODE_TTY) && TYPE(mrb_stdout) == T_FILE) {
        mrb_io_t *ofp;
GetOpenFile(mrb_state *mrb, mrb_stdout, ofp);
        if (ofp->mode & FMODE_TTY) {
mrb_io_flush(mrb_state *mrb, mrb_stdout);
        }
    }
    if (io_fillbuf(fptr) < 0) {
	return Qnil;
    }
    fptr->rbuf.off++;
    fptr->rbuf.len--;
    c = (unsigned char)fptr->rbuf.ptr[fptr->rbuf.off-1];
    return INT2FIX(c & 0xff);
}

/*
 *  call-seq:
 *     ios.readbyte   -> fixnum
 *
 *  Reads a byte as with <code>IO#getbyte</code>, but raises an
 *  <code>EOFError</code> on end of file.
 */

static mrb_value
mrb_io_readbyte(mrb_state *mrb, mrb_value io)
{
    mrb_value c = mrb_io_getbyte(io);

    if (NIL_P(c)) {
	mrb_eof_error(mrb_state *mrb, );
    }
    return c;
}

/*
 *  call-seq:
 *     ios.ungetbyte(string)   -> nil
 *     ios.ungetbyte(integer)   -> nil
 *
 *  Pushes back bytes (passed as a parameter) onto <em>ios</em>,
 *  such that a subsequent buffered read will return it. Only one byte
 *  may be pushed back before a subsequent read operation (that is,
 *  you will be able to read only the last of several bytes that have been pushed
 *  back). Has no effect with unbuffered reads (such as <code>IO#sysread</code>).
 *
 *     f = File.new("testfile")   #=> #<File:testfile>
 *     b = f.getbyte              #=> 0x38
 *     f.ungetbyte(b)             #=> nil
 *     f.getbyte                  #=> 0x38
 */

mrb_value
mrb_io_ungetbyte(mrb_state *mrb, mrb_value io, mrb_value b)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_byte_readable(mrb_state *mrb, fptr);
    if (NIL_P(b)) return Qnil;
    if (FIXNUM_P(b)) {
	char cc = FIX2INT(b);
	b = mrb_str_new(&cc, 1);
    }
    else {
	SafeStringValue(mrb_state *mrb, b);
    }
io_ungetbyte(mrb_state *mrb, b, fptr);
    return Qnil;
}

/*
 *  call-seq:
 *     ios.ungetc(string)   -> nil
 *
 *  Pushes back one character (passed as a parameter) onto <em>ios</em>,
 *  such that a subsequent buffered character read will return it. Only one character
 *  may be pushed back before a subsequent read operation (that is,
 *  you will be able to read only the last of several characters that have been pushed
 *  back). Has no effect with unbuffered reads (such as <code>IO#sysread</code>).
 *
 *     f = File.new("testfile")   #=> #<File:testfile>
 *     c = f.getc                 #=> "8"
 *     f.ungetc(c)                #=> nil
 *     f.getc                     #=> "8"
 */

mrb_value
mrb_io_ungetc(mrb_state *mrb, mrb_value io, mrb_value c)
{
    mrb_io_t *fptr;
    long len;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_char_readable(mrb_state *mrb, fptr);
    if (NIL_P(c)) return Qnil;
    if (FIXNUM_P(c)) {
	c = mrb_enc_uint_chr(FIX2UINT(c), io_read_encoding(fptr));
    }
    else if (TYPE(c) == T_BIGNUM) {
	c = mrb_enc_uint_chr(NUM2UINT(c), io_read_encoding(fptr));
    }
    else {
	SafeStringValue(mrb_state *mrb, c);
    }
    if (NEED_READCONV(fptr)) {
	SET_BINARY_MODE(mrb_state *mrb, fptr);
        len = RSTRING_LEN(c);
#if SIZEOF_LONG > SIZEOF_INT
	if (len > INT_MAX)
	    mrb_raise(mrb_eIOError, "ungetc failed");
#endif
make_readconv(mrb_state *mrb, fptr, (int)len);
        if (fptr->cbuf.capa - fptr->cbuf.len < len)
mrb_raise(mrb_state *mrb, mrb_eIOError, "ungetc failed");
        if (fptr->cbuf.off < len) {
MEMMOVE(mrb_state *mrb, fptr->cbuf.ptr+fptr->cbuf.capa-fptr->cbuf.len,
                    fptr->cbuf.ptr+fptr->cbuf.off,
                    char, fptr->cbuf.len);
            fptr->cbuf.off = fptr->cbuf.capa-fptr->cbuf.len;
        }
        fptr->cbuf.off -= (int)len;
        fptr->cbuf.len += (int)len;
MEMMOVE(mrb_state *mrb, fptr->cbuf.ptr+fptr->cbuf.off, RSTRING_PTR(c), char, len);
    }
    else {
	NEED_NEWLINE_DECORATOR_ON_READ_CHECK(mrb_state *mrb, fptr);
io_ungetbyte(mrb_state *mrb, c, fptr);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     ios.isatty   -> true or false
 *     ios.tty?     -> true or false
 *
 *  Returns <code>true</code> if <em>ios</em> is associated with a
 *  terminal device (tty), <code>false</code> otherwise.
 *
 *     File.new("testfile").isatty   #=> false
 *     File.new("/dev/tty").isatty   #=> true
 */

static mrb_value
mrb_io_isatty(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
    if (isatty(fptr->fd) == 0)
	return Qfalse;
    return Qtrue;
}

#if defined(HAVE_FCNTL) && defined(F_GETFD) && defined(F_SETFD) && defined(FD_CLOEXEC)
/*
 *  call-seq:
 *     ios.close_on_exec?   -> true or false
 *
 *  Returns <code>true</code> if <em>ios</em> will be closed on exec.
 *
 *     f = open("/dev/null")
 *     f.close_on_exec?                 #=> false
 *     f.close_on_exec = true
 *     f.close_on_exec?                 #=> true
 *     f.close_on_exec = false
 *     f.close_on_exec?                 #=> false
 */

static mrb_value
mrb_io_close_on_exec_p(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_value write_io;
    int fd, ret;

    write_io = GetWriteIO(io);
    if (io != write_io) {
GetOpenFile(mrb_state *mrb, write_io, fptr);
        if (fptr && 0 <= (fd = fptr->fd)) {
            if ((ret = fcntl(fd, F_GETFD)) == -1) mrb_sys_fail_path(fptr->pathv);
            if (!(ret & FD_CLOEXEC)) return Qfalse;
        }
    }

GetOpenFile(mrb_state *mrb, io, fptr);
    if (fptr && 0 <= (fd = fptr->fd)) {
        if ((ret = fcntl(fd, F_GETFD)) == -1) mrb_sys_fail_path(fptr->pathv);
        if (!(ret & FD_CLOEXEC)) return Qfalse;
    }
    return Qtrue;
}
#else
#define mrb_io_close_on_exec_p mrb_f_notimplement
#endif

#if defined(HAVE_FCNTL) && defined(F_GETFD) && defined(F_SETFD) && defined(FD_CLOEXEC)
/*
 *  call-seq:
 *     ios.close_on_exec = bool    -> true or false
 *
 *  Sets a close-on-exec flag.
 *
 *     f = open("/dev/null")
 *     f.close_on_exec = true
 *     system("cat", "/proc/self/fd/#{f.fileno}") # cat: /proc/self/fd/3: No such file or directory
 *     f.closed?                #=> false
 */

static mrb_value
mrb_io_set_close_on_exec(mrb_state *mrb, mrb_value io, mrb_value arg)
{
    int flag = RTEST(arg) ? FD_CLOEXEC : 0;
    mrb_io_t *fptr;
    mrb_value write_io;
    int fd, ret;

    write_io = GetWriteIO(io);
    if (io != write_io) {
GetOpenFile(mrb_state *mrb, write_io, fptr);
        if (fptr && 0 <= (fd = fptr->fd)) {
            if ((ret = fcntl(fptr->fd, F_GETFD)) == -1) mrb_sys_fail_path(fptr->pathv);
            if ((ret & FD_CLOEXEC) != flag) {
                ret = (ret & ~FD_CLOEXEC) | flag;
                ret = fcntl(fd, F_SETFD, ret);
                if (ret == -1) mrb_sys_fail_path(fptr->pathv);
            }
        }

    }

GetOpenFile(mrb_state *mrb, io, fptr);
    if (fptr && 0 <= (fd = fptr->fd)) {
        if ((ret = fcntl(fd, F_GETFD)) == -1) mrb_sys_fail_path(fptr->pathv);
        if ((ret & FD_CLOEXEC) != flag) {
            ret = (ret & ~FD_CLOEXEC) | flag;
            ret = fcntl(fd, F_SETFD, ret);
            if (ret == -1) mrb_sys_fail_path(fptr->pathv);
        }
    }
    return Qnil;
}
#else
#define mrb_io_set_close_on_exec mrb_f_notimplement
#endif

#define FMODE_PREP (1<<16)
#define IS_PREP_STDIO(f) ((f)->mode & FMODE_PREP)
#define PREP_STDIO_NAME(f) (RSTRING_PTR((f)->pathv))

static mrb_value
finish_writeconv(mrb_state *mrb, mrb_io_t *fptr, int noalloc)
{
    unsigned char *ds, *dp, *de;
    mrb_econv_result_t res;

    if (!fptr->wbuf.ptr) {
        unsigned char buf[1024];
        long r;

        res = econv_destination_buffer_full;
        while (res == econv_destination_buffer_full) {
            ds = dp = buf;
            de = buf + sizeof(buf);
            res = mrb_econv_convert(fptr->writeconv, NULL, NULL, &dp, de, 0);
            while (dp-ds) {
              retry:
                r = mrb_write_internal(fptr->fd, ds, dp-ds);
                if (r == dp-ds)
                    break;
                if (0 <= r) {
                    ds += r;
                }
                if (mrb_io_wait_writable(fptr->fd)) {
                    if (fptr->fd < 0)
                        return noalloc ? Qtrue : mrb_exc_new3(mrb_eIOError, mrb_str_new_cstr("closed stream"));
                    goto retry;
                }
                return noalloc ? Qtrue : INT2NUM(errno);
            }
            if (res == econv_invalid_byte_sequence ||
                res == econv_incomplete_input ||
                res == econv_undefined_conversion) {
                return noalloc ? Qtrue : mrb_econv_make_exception(fptr->writeconv);
            }
        }

        return Qnil;
    }

    res = econv_destination_buffer_full;
    while (res == econv_destination_buffer_full) {
        if (fptr->wbuf.len == fptr->wbuf.capa) {
            if (io_fflush(fptr) < 0)
                return noalloc ? Qtrue : INT2NUM(errno);
        }

        ds = dp = (unsigned char *)fptr->wbuf.ptr + fptr->wbuf.off + fptr->wbuf.len;
        de = (unsigned char *)fptr->wbuf.ptr + fptr->wbuf.capa;
        res = mrb_econv_convert(fptr->writeconv, NULL, NULL, &dp, de, 0);
        fptr->wbuf.len += (int)(dp - ds);
        if (res == econv_invalid_byte_sequence ||
            res == econv_incomplete_input ||
            res == econv_undefined_conversion) {
            return noalloc ? Qtrue : mrb_econv_make_exception(fptr->writeconv);
        }
    }
    return Qnil;
}

struct finish_writeconv_arg {
    mrb_io_t *fptr;
    int noalloc;
};

static mrb_value
finish_writeconv_sync(mrb_state *mrb, mrb_value arg)
{
    struct finish_writeconv_arg *p = (struct finish_writeconv_arg *)arg;
    return finish_writeconv(p->fptr, p->noalloc);
}

static void
fptr_finalize(mrb_state *mrb, mrb_io_t *fptr, int noraise)
{
    mrb_value err = Qnil;
    if (fptr->writeconv) {
	if (fptr->write_lock && !noraise) {
            struct finish_writeconv_arg arg;
            arg.fptr = fptr;
            arg.noalloc = noraise;
            err = mrb_mutex_synchronize(fptr->write_lock, finish_writeconv_sync, (mrb_value)&arg);
	}
	else {
	    err = finish_writeconv(fptr, noraise);
	}
    }
    if (fptr->wbuf.len) {
	if (noraise) {
	    if ((int)io_flush_buffer_sync(fptr) < 0 && NIL_P(err))
		err = Qtrue;
	}
	else {
	    if (io_fflush(fptr) < 0 && NIL_P(err))
		err = INT2NUM(errno);
	}
    }
    if (IS_PREP_STDIO(fptr) || fptr->fd <= 2) {
        goto skip_fd_close;
    }
    if (fptr->stdio_file) {
        /* fptr->stdio_file is deallocated anyway
         * even if fclose failed.  */
        if (fclose(fptr->stdio_file) < 0 && NIL_P(err))
            err = noraise ? Qtrue : INT2NUM(errno);
    }
    else if (0 <= fptr->fd) {
        /* fptr->fd may be closed even if close fails.
         * POSIX doesn't specify it.
         * We assumes it is closed.  */
        if (close(fptr->fd) < 0 && NIL_P(err))
            err = noraise ? Qtrue : INT2NUM(errno);
    }
  skip_fd_close:
    fptr->fd = -1;
    fptr->stdio_file = 0;
    fptr->mode &= ~(FMODE_READABLE|FMODE_WRITABLE);

    if (!NIL_P(err) && !noraise) {
switch(mrb_state *mrb, TYPE(err)) {
          case T_FIXNUM:
          case T_BIGNUM:
            errno = NUM2INT(err);
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);

          default:
mrb_exc_raise(mrb_state *mrb, err);
        }
    }
}

static void
mrb_io_fptr_cleanup(mrb_state *mrb, mrb_io_t *fptr, int noraise)
{
    if (fptr->finalize) {
	(mrb_state *mrb, *fptr->finalize)(fptr, noraise);
    }
    else {
	fptr_finalize(mrb_state *mrb, fptr, noraise);
    }
}

static void
clear_readconv(mrb_state *mrb, mrb_io_t *fptr)
{
    if (fptr->readconv) {
mrb_econv_close(mrb_state *mrb, fptr->readconv);
        fptr->readconv = NULL;
    }
    if (fptr->cbuf.ptr) {
free(mrb_state *mrb, fptr->cbuf.ptr);
        fptr->cbuf.ptr = NULL;
    }
}

static void
clear_writeconv(mrb_state *mrb, mrb_io_t *fptr)
{
    if (fptr->writeconv) {
mrb_econv_close(mrb_state *mrb, fptr->writeconv);
        fptr->writeconv = NULL;
    }
    fptr->writeconv_initialized = 0;
}

static void
clear_codeconv(mrb_state *mrb, mrb_io_t *fptr)
{
clear_readconv(mrb_state *mrb, fptr);
clear_writeconv(mrb_state *mrb, fptr);
}

int
mrb_io_fptr_finalize(mrb_state *mrb, mrb_io_t *fptr)
{
    if (!fptr) return 0;
    fptr->pathv = Qnil;
    if (0 <= fptr->fd)
	mrb_io_fptr_cleanup(mrb_state *mrb, fptr, TRUE);
    fptr->write_lock = 0;
    if (fptr->rbuf.ptr) {
free(mrb_state *mrb, fptr->rbuf.ptr);
        fptr->rbuf.ptr = 0;
    }
    if (fptr->wbuf.ptr) {
free(mrb_state *mrb, fptr->wbuf.ptr);
        fptr->wbuf.ptr = 0;
    }
clear_codeconv(mrb_state *mrb, fptr);
free(mrb_state *mrb, fptr);
    return 1;
}

size_t mrb_econv_memsize(mrb_econv_t *);

RUBY_FUNC_EXPORTED size_t
mrb_io_memsize(mrb_state *mrb, const mrb_io_t *fptr)
{
    size_t size = sizeof(mrb_io_t);
    size += fptr->rbuf.capa;
    size += fptr->wbuf.capa;
    size += fptr->cbuf.capa;
    if (fptr->readconv) size += mrb_econv_memsize(fptr->readconv);
    if (fptr->writeconv) size += mrb_econv_memsize(fptr->writeconv);
    return size;
}

mrb_value
mrb_io_close(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    int fd;
    mrb_value write_io;
    mrb_io_t *write_fptr;

    write_io = GetWriteIO(io);
    if (io != write_io) {
        write_fptr = RFILE(write_io)->fptr;
        if (write_fptr && 0 <= write_fptr->fd) {
mrb_io_fptr_cleanup(mrb_state *mrb, write_fptr, TRUE);
        }
    }

    fptr = RFILE(io)->fptr;
    if (!fptr) return Qnil;
    if (fptr->fd < 0) return Qnil;

    fd = fptr->fd;
#if defined __APPLE__ && defined(__MACH__) && \
(mrb_state *mrb, !defined(MAC_OS_X_VERSION_MIN_ALLOWED) || MAC_OS_X_VERSION_MIN_ALLOWED <= 1050)
    /* close(2) on a fd which is being read by another thread causes
     * deadlock on Mac OS X 10.5 */
mrb_thread_fd_close(mrb_state *mrb, fd);
#endif
mrb_io_fptr_cleanup(mrb_state *mrb, fptr, FALSE);
mrb_thread_fd_close(mrb_state *mrb, fd);

    if (fptr->pid) {
	mrb_syswait(mrb_state *mrb, fptr->pid);
	fptr->pid = 0;
    }

    return Qnil;
}

/*
 *  call-seq:
 *     ios.close   -> nil
 *
 *  Closes <em>ios</em> and flushes any pending writes to the operating
 *  system. The stream is unavailable for any further data operations;
 *  an <code>IOError</code> is raised if such an attempt is made. I/O
 *  streams are automatically closed when they are claimed by the
 *  garbage collector.
 *
 *  If <em>ios</em> is opened by <code>IO.popen</code>,
 *  <code>close</code> sets <code>$?</code>.
 */

static mrb_value
mrb_io_close_m(mrb_state *mrb, mrb_value io)
{
    if (mrb_safe_level() >= 4 && !OBJ_UNTRUSTED(io)) {
	mrb_raise(mrb_state *mrb, mrb_eSecurityError, "Insecure: can't close");
    }
mrb_io_check_closed(mrb_state *mrb, RFILE(io)->fptr);
mrb_io_close(mrb_state *mrb, io);
    return Qnil;
}

static mrb_value
io_call_close(mrb_state *mrb, mrb_value io)
{
    return mrb_funcall(io, mrb_intern("close"), 0, 0);
}

static mrb_value
io_close(mrb_state *mrb, mrb_value io)
{
    return mrb_rescue(io_call_close, io, 0, 0);
}

/*
 *  call-seq:
 *     ios.closed?    -> true or false
 *
 *  Returns <code>true</code> if <em>ios</em> is completely closed (for
 *  duplex streams, both reader and writer), <code>false</code>
 *  otherwise.
 *
 *     f = File.new("testfile")
 *     f.close         #=> nil
 *     f.closed?       #=> true
 *     f = IO.popen("/bin/sh","r+")
 *     f.close_write   #=> nil
 *     f.closed?       #=> false
 *     f.close_read    #=> nil
 *     f.closed?       #=> true
 */


static mrb_value
mrb_io_closed(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_value write_io;
    mrb_io_t *write_fptr;

    write_io = GetWriteIO(io);
    if (io != write_io) {
        write_fptr = RFILE(write_io)->fptr;
        if (write_fptr && 0 <= write_fptr->fd) {
            return Qfalse;
        }
    }

    fptr = RFILE(io)->fptr;
mrb_io_check_initialized(mrb_state *mrb, fptr);
    return 0 <= fptr->fd ? Qfalse : Qtrue;
}

/*
 *  call-seq:
 *     ios.close_read    -> nil
 *
 *  Closes the read end of a duplex I/O stream (i.e., one that contains
 *  both a read and a write stream, such as a pipe). Will raise an
 *  <code>IOError</code> if the stream is not duplexed.
 *
 *     f = IO.popen("/bin/sh","r+")
 *     f.close_read
 *     f.readlines
 *
 *  <em>produces:</em>
 *
 *     prog.rb:3:in `readlines': not opened for reading (IOError)
 *     	from prog.rb:3
 */

static mrb_value
mrb_io_close_read(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_value write_io;

    if (mrb_safe_level() >= 4 && !OBJ_UNTRUSTED(io)) {
	mrb_raise(mrb_state *mrb, mrb_eSecurityError, "Insecure: can't close");
    }
GetOpenFile(mrb_state *mrb, io, fptr);
    if (is_socket(fptr->fd, fptr->pathv)) {
#ifndef SHUT_RD
# define SHUT_RD 0
#endif
        if (shutdown(fptr->fd, SHUT_RD) < 0)
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
        fptr->mode &= ~FMODE_READABLE;
        if (!(fptr->mode & FMODE_WRITABLE))
            return mrb_io_close(io);
        return Qnil;
    }

    write_io = GetWriteIO(io);
    if (io != write_io) {
	mrb_io_t *wfptr;
mrb_io_fptr_cleanup(mrb_state *mrb, fptr, FALSE);
	GetOpenFile(mrb_state *mrb, write_io, wfptr);
RFILE(mrb_state *mrb, io)->fptr = wfptr;
RFILE(mrb_state *mrb, write_io)->fptr = NULL;
mrb_io_fptr_finalize(mrb_state *mrb, fptr);
        return Qnil;
    }

    if (fptr->mode & FMODE_WRITABLE) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "closing non-duplex IO for reading");
    }
    return mrb_io_close(io);
}

/*
 *  call-seq:
 *     ios.close_write   -> nil
 *
 *  Closes the write end of a duplex I/O stream (i.e., one that contains
 *  both a read and a write stream, such as a pipe). Will raise an
 *  <code>IOError</code> if the stream is not duplexed.
 *
 *     f = IO.popen("/bin/sh","r+")
 *     f.close_write
 *     f.print "nowhere"
 *
 *  <em>produces:</em>
 *
 *     prog.rb:3:in `write': not opened for writing (IOError)
 *     	from prog.rb:3:in `print'
 *     	from prog.rb:3
 */

static mrb_value
mrb_io_close_write(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_value write_io;

    if (mrb_safe_level() >= 4 && !OBJ_UNTRUSTED(io)) {
	mrb_raise(mrb_state *mrb, mrb_eSecurityError, "Insecure: can't close");
    }
    write_io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, write_io, fptr);
    if (is_socket(fptr->fd, fptr->pathv)) {
#ifndef SHUT_WR
# define SHUT_WR 1
#endif
        if (shutdown(fptr->fd, SHUT_WR) < 0)
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
        fptr->mode &= ~FMODE_WRITABLE;
        if (!(fptr->mode & FMODE_READABLE))
	    return mrb_io_close(write_io);
        return Qnil;
    }

    if (fptr->mode & FMODE_READABLE) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "closing non-duplex IO for writing");
    }

mrb_io_close(mrb_state *mrb, write_io);
    if (io != write_io) {
	GetOpenFile(mrb_state *mrb, io, fptr);
	fptr->tied_io_for_writing = 0;
	fptr->mode &= ~FMODE_DUPLEX;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     ios.sysseek(offset, whence=IO::SEEK_SET)   -> integer
 *
 *  Seeks to a given <i>offset</i> in the stream according to the value
 *  of <i>whence</i> (see <code>IO#seek</code> for values of
 *  <i>whence</i>). Returns the new offset into the file.
 *
 *     f = File.new("testfile")
 *     f.sysseek(-13, IO::SEEK_END)   #=> 53
 *     f.sysread(10)                  #=> "And so on."
 */

static mrb_value
mrb_io_sysseek(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value offset, ptrname;
    int whence = SEEK_SET;
    mrb_io_t *fptr;
    off_t pos;

    if (mrb_scan_args(argc, argv, "11", &offset, &ptrname) == 2) {
	whence = NUM2INT(ptrname);
    }
    pos = NUM2OFFT(offset);
GetOpenFile(mrb_state *mrb, io, fptr);
    if ((fptr->mode & FMODE_READABLE) &&
(mrb_state *mrb, READ_DATA_BUFFERED(fptr) || READ_CHAR_PENDING(fptr))) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "sysseek for buffered IO");
    }
    if ((fptr->mode & FMODE_WRITABLE) && fptr->wbuf.len) {
	mrb_warn(mrb_state *mrb, "sysseek for buffered IO");
    }
    errno = 0;
    pos = lseek(fptr->fd, pos, whence);
    if (pos == -1 && errno) mrb_sys_fail_path(fptr->pathv);

    return OFFT2NUM(pos);
}

/*
 *  call-seq:
 *     ios.syswrite(string)   -> integer
 *
 *  Writes the given string to <em>ios</em> using a low-level write.
 *  Returns the number of bytes written. Do not mix with other methods
 *  that write to <em>ios</em> or you may get unpredictable results.
 *  Raises <code>SystemCallError</code> on error.
 *
 *     f = File.new("out", "w")
 *     f.syswrite("ABCDEF")   #=> 6
 */

static mrb_value
mrb_io_syswrite(mrb_state *mrb, mrb_value io, mrb_value str)
{
    mrb_io_t *fptr;
    long n;

mrb_secure(mrb_state *mrb, 4);
    if (TYPE(str) != T_STRING)
	str = mrb_obj_as_string(str);

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_writable(mrb_state *mrb, fptr);

    if (fptr->wbuf.len) {
	mrb_warn(mrb_state *mrb, "syswrite for buffered IO");
    }
    if (!mrb_thread_fd_writable(fptr->fd)) {
mrb_io_check_closed(mrb_state *mrb, fptr);
    }

    n = mrb_write_internal(fptr->fd, RSTRING_PTR(str), RSTRING_LEN(str));

    if (n == -1) mrb_sys_fail_path(fptr->pathv);

    return LONG2FIX(n);
}

/*
 *  call-seq:
 *     ios.sysread(maxlen[, outbuf])    -> string
 *
 *  Reads <i>maxlen</i> bytes from <em>ios</em> using a low-level
 *  read and returns them as a string.  Do not mix with other methods
 *  that read from <em>ios</em> or you may get unpredictable results.
 *  If the optional <i>outbuf</i> argument is present, it must reference
 *  a String, which will receive the data.
 *  Raises <code>SystemCallError</code> on error and
 *  <code>EOFError</code> at end of file.
 *
 *     f = File.new("testfile")
 *     f.sysread(16)   #=> "This is line one"
 */

static mrb_value
mrb_io_sysread(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value len, str;
    mrb_io_t *fptr;
    long n, ilen;

mrb_scan_args(mrb_state *mrb, argc, argv, "11", &len, &str);
    ilen = NUM2LONG(len);

io_setstrbuf(mrb_state *mrb, &str,ilen);
    if (ilen == 0) return str;

GetOpenFile(mrb_state *mrb, io, fptr);
mrb_io_check_byte_readable(mrb_state *mrb, fptr);

    if (READ_DATA_BUFFERED(fptr)) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "sysread for buffered IO");
    }

    n = fptr->fd;
mrb_thread_wait_fd(mrb_state *mrb, fptr->fd);
mrb_io_check_closed(mrb_state *mrb, fptr);

mrb_str_locktmp(mrb_state *mrb, str);
    n = mrb_read_internal(fptr->fd, RSTRING_PTR(str), ilen);
mrb_str_unlocktmp(mrb_state *mrb, str);

    if (n == -1) {
	mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
    }
mrb_str_set_len(mrb_state *mrb, str, n);
    if (n == 0 && ilen > 0) {
	mrb_eof_error(mrb_state *mrb, );
    }
mrb_str_resize(mrb_state *mrb, str, n);
OBJ_TAINT(mrb_state *mrb, str);

    return str;
}

mrb_value
mrb_io_binmode(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
    if (fptr->readconv)
mrb_econv_binmode(mrb_state *mrb, fptr->readconv);
    if (fptr->writeconv)
mrb_econv_binmode(mrb_state *mrb, fptr->writeconv);
    fptr->mode |= FMODE_BINMODE;
    fptr->mode &= ~FMODE_TEXTMODE;
    fptr->writeconv_pre_ecflags &= ~ECONV_NEWLINE_DECORATOR_MASK;
#ifdef O_BINARY
    if (!fptr->readconv) {
	SET_BINARY_MODE_WITH_SEEK_CUR(mrb_state *mrb, fptr);
    }
    else {
	setmode(mrb_state *mrb, fptr->fd, O_BINARY);
    }
#endif
    return io;
}

mrb_value
mrb_io_ascii8bit_binmode(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
    if (fptr->readconv) {
mrb_econv_close(mrb_state *mrb, fptr->readconv);
        fptr->readconv = NULL;
    }
    if (fptr->writeconv) {
mrb_econv_close(mrb_state *mrb, fptr->writeconv);
        fptr->writeconv = NULL;
    }
    fptr->mode |= FMODE_BINMODE;
    fptr->mode &= ~FMODE_TEXTMODE;
SET_BINARY_MODE_WITH_SEEK_CUR(mrb_state *mrb, fptr);

    fptr->encs.enc = mrb_ascii8bit_encoding();
    fptr->encs.enc2 = NULL;
    fptr->encs.ecflags = 0;
    fptr->encs.ecopts = Qnil;
clear_codeconv(mrb_state *mrb, fptr);

    return io;
}

/*
 *  call-seq:
 *     ios.binmode    -> ios
 *
 *  Puts <em>ios</em> into binary mode.
 *  Once a stream is in binary mode, it cannot be reset to nonbinary mode.
 *
 *  - newline conversion disabled
 *  - encoding conversion disabled
 *  - content is treated as ASCII-8BIT
 *
 */

static mrb_value
mrb_io_binmode_m(mrb_state *mrb, mrb_value io)
{
    mrb_value write_io;

mrb_io_ascii8bit_binmode(mrb_state *mrb, io);

    write_io = GetWriteIO(io);
    if (write_io != io)
mrb_io_ascii8bit_binmode(mrb_state *mrb, write_io);
    return io;
}

/*
 *  call-seq:
 *     ios.binmode?    -> true or false
 *
 *  Returns <code>true</code> if <em>ios</em> is binmode.
 */
static mrb_value
mrb_io_binmode_p(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
GetOpenFile(mrb_state *mrb, io, fptr);
    return fptr->mode & FMODE_BINMODE ? Qtrue : Qfalse;
}

static const char*
mrb_io_fmode_modestr(mrb_state *mrb, int fmode)
{
    if (fmode & FMODE_APPEND) {
	if ((fmode & FMODE_READWRITE) == FMODE_READWRITE) {
	    return MODE_BTMODE("a+", "ab+", "at+");
	}
	return MODE_BTMODE("a", "ab", "at");
    }
    switch (fmode & FMODE_READWRITE) {
      case FMODE_READABLE:
	return MODE_BTMODE("r", "rb", "rt");
      case FMODE_WRITABLE:
	return MODE_BTMODE("w", "wb", "wt");
      case FMODE_READWRITE:
	if (fmode & FMODE_CREATE) {
	    return MODE_BTMODE("w+", "wb+", "wt+");
	}
	return MODE_BTMODE("r+", "rb+", "rt+");
    }
mrb_raise(mrb_state *mrb, mrb_eArgError, "invalid access fmode 0x%x", fmode);
    return NULL;		/* not reached */
}

static int
io_encname_bom_p(mrb_state *mrb, const char *name, long len)
{
    static const char bom_prefix[] = "bom|utf-";
    enum {bom_prefix_len = (int)sizeof(bom_prefix) - 1};
    if (!len) {
	const char *p = strchr(name, ':');
	len = p ? (long)(p - name) : (long)strlen(name);
    }
    return len > bom_prefix_len && STRNCASECMP(name, bom_prefix, bom_prefix_len) == 0;
}

int
mrb_io_modestr_fmode(mrb_state *mrb, const char *modestr)
{
    int fmode = 0;
    const char *m = modestr, *p = NULL;

    switch (*m++) {
      case 'r':
	fmode |= FMODE_READABLE;
	break;
      case 'w':
	fmode |= FMODE_WRITABLE | FMODE_TRUNC | FMODE_CREATE;
	break;
      case 'a':
	fmode |= FMODE_WRITABLE | FMODE_APPEND | FMODE_CREATE;
	break;
      default:
      error:
	mrb_raise(mrb_state *mrb, mrb_eArgError, "invalid access mode %s", modestr);
    }

    while (*m) {
        switch (*m++) {
	  case 'b':
            fmode |= FMODE_BINMODE;
            break;
	  case 't':
            fmode |= FMODE_TEXTMODE;
            break;
	  case '+':
            fmode |= FMODE_READWRITE;
            break;
	  default:
            goto error;
	  case ':':
	    p = m;
            goto finished;
        }
    }

  finished:
    if ((fmode & FMODE_BINMODE) && (fmode & FMODE_TEXTMODE))
        goto error;
    if (p && io_encname_bom_p(p, 0))
	fmode |= FMODE_SETENC_BY_BOM;

    return fmode;
}

int
mrb_io_oflags_fmode(mrb_state *mrb, int oflags)
{
    int fmode = 0;

    switch (oflags & (O_RDONLY|O_WRONLY|O_RDWR)) {
      case O_RDONLY:
	fmode = FMODE_READABLE;
	break;
      case O_WRONLY:
	fmode = FMODE_WRITABLE;
	break;
      case O_RDWR:
	fmode = FMODE_READWRITE;
	break;
    }

    if (oflags & O_APPEND) {
	fmode |= FMODE_APPEND;
    }
    if (oflags & O_TRUNC) {
	fmode |= FMODE_TRUNC;
    }
    if (oflags & O_CREAT) {
	fmode |= FMODE_CREATE;
    }
#ifdef O_BINARY
    if (oflags & O_BINARY) {
	fmode |= FMODE_BINMODE;
    }
#endif

    return fmode;
}

static int
mrb_io_fmode_oflags(mrb_state *mrb, int fmode)
{
    int oflags = 0;

    switch (fmode & FMODE_READWRITE) {
      case FMODE_READABLE:
        oflags |= O_RDONLY;
        break;
      case FMODE_WRITABLE:
        oflags |= O_WRONLY;
        break;
      case FMODE_READWRITE:
        oflags |= O_RDWR;
        break;
    }

    if (fmode & FMODE_APPEND) {
        oflags |= O_APPEND;
    }
    if (fmode & FMODE_TRUNC) {
        oflags |= O_TRUNC;
    }
    if (fmode & FMODE_CREATE) {
        oflags |= O_CREAT;
    }
#ifdef O_BINARY
    if (fmode & FMODE_BINMODE) {
        oflags |= O_BINARY;
    }
#endif

    return oflags;
}

int
mrb_io_modestr_oflags(mrb_state *mrb, const char *modestr)
{
    return mrb_io_fmode_oflags(mrb_io_modestr_fmode(modestr));
}

static const char*
mrb_io_oflags_modestr(mrb_state *mrb, int oflags)
{
#ifdef O_BINARY
# define MODE_BINARY(a,b) ((oflags & O_BINARY) ? (b) : (a))
#else
# define MODE_BINARY(a,b) (a)
#endif
    int accmode = oflags & (O_RDONLY|O_WRONLY|O_RDWR);
    if (oflags & O_APPEND) {
	if (accmode == O_WRONLY) {
	    return MODE_BINARY("a", "ab");
	}
	if (accmode == O_RDWR) {
	    return MODE_BINARY("a+", "ab+");
	}
    }
    switch (oflags & (O_RDONLY|O_WRONLY|O_RDWR)) {
      case O_RDONLY:
	return MODE_BINARY("r", "rb");
      case O_WRONLY:
	return MODE_BINARY("w", "wb");
      case O_RDWR:
	return MODE_BINARY("r+", "rb+");
    }
mrb_raise(mrb_state *mrb, mrb_eArgError, "invalid access oflags 0x%x", oflags);
    return NULL;		/* not reached */
}

/*
 * Convert external/internal encodings to enc/enc2
 * NULL => use default encoding
 * Qnil => no encoding specified (internal only)
 */
static void
mrb_io_ext_int_to_encs(mrb_state *mrb, mrb_encoding *ext, mrb_encoding *intern, mrb_encoding **enc, mrb_encoding **enc2)
{
    int default_ext = 0;

    if (ext == NULL) {
	ext = mrb_default_external_encoding();
	default_ext = 1;
    }
    if (intern == NULL && ext != mrb_ascii8bit_encoding())
	/* If external is ASCII-8BIT, no default transcoding */
	intern = mrb_default_internal_encoding();
    if (intern == NULL || intern == (mrb_encoding *)Qnil || intern == ext) {
	/* No internal encoding => use external + no transcoding */
	*enc = (default_ext && intern != ext) ? NULL : ext;
	*enc2 = NULL;
    }
    else {
	*enc = intern;
	*enc2 = ext;
    }
}

static void
parse_mode_enc(mrb_state *mrb, const char *estr, mrb_encoding **enc_p, mrb_encoding **enc2_p, int *fmode_p)
{
    const char *p;
    char encname[ENCODING_MAXNAMELEN+1];
    int idx, idx2;
    mrb_encoding *ext_enc, *int_enc;

    /* parse estr as "enc" or "enc2:enc" or "enc:-" */

    p = strrchr(estr, ':');
    if (p) {
	long len = (p++) - estr;
	if (len == 0 || len > ENCODING_MAXNAMELEN)
	    idx = -1;
	else {
	    if (io_encname_bom_p(estr, len)) {
		if (fmode_p) *fmode_p |= FMODE_SETENC_BY_BOM;
		estr += 4;
                len -= 4;
            }
	    memcpy(encname, estr, len);
	    encname[len] = '\0';
	    estr = encname;
	    idx = mrb_enc_find_index(encname);
	}
    }
    else {
	long len = strlen(estr);
	if (io_encname_bom_p(estr, len)) {
	    if (fmode_p) *fmode_p |= FMODE_SETENC_BY_BOM;
	    estr += 4;
            len -= 4;
	    memcpy(encname, estr, len);
	    encname[len] = '\0';
	    estr = encname;
	}
	idx = mrb_enc_find_index(estr);
    }

    if (idx >= 0)
	ext_enc = mrb_enc_from_index(idx);
    else {
	if (idx != -2)
	    mrb_warn("Unsupported encoding %s ignored", estr);
	ext_enc = NULL;
    }

    int_enc = NULL;
    if (p) {
	if (*p == '-' && *(p+1) == '\0') {
	    /* Special case - "-" => no transcoding */
	    int_enc = (mrb_encoding *)Qnil;
	}
	else {
	    idx2 = mrb_enc_find_index(p);
	    if (idx2 < 0)
		mrb_warn(mrb_state *mrb, "Unsupported encoding %s ignored", p);
	    else if (idx2 == idx) {
		mrb_warn(mrb_state *mrb, "Ignoring internal encoding %s: it is identical to external encoding %s", p, estr);
		int_enc = (mrb_encoding *)Qnil;
	    }
	    else
		int_enc = mrb_enc_from_index(idx2);
	}
    }

mrb_io_ext_int_to_encs(mrb_state *mrb, ext_enc, int_enc, enc_p, enc2_p);
}

static void
mode_enc(mrb_state *mrb, mrb_io_t *fptr, const char *estr)
{
clear_codeconv(mrb_state *mrb, fptr);

parse_mode_enc(mrb_state *mrb, estr, &fptr->encs.enc, &fptr->encs.enc2, NULL);
}

static void
mrb_io_mode_enc(mrb_state *mrb, mrb_io_t *fptr, const char *modestr)
{
    const char *p = strchr(modestr, ':');
    if (p) {
	mode_enc(mrb_state *mrb, fptr, p+1);
    }
}

int
mrb_io_extract_encoding_option(mrb_state *mrb, mrb_value opt, mrb_encoding **enc_p, mrb_encoding **enc2_p, int *fmode_p)
{
    mrb_value encoding=Qnil, extenc=Qundef, intenc=Qundef, tmp;
    int extracted = 0;
    mrb_encoding *extencoding = NULL;
    mrb_encoding *intencoding = NULL;

    if (!NIL_P(opt)) {
	mrb_value v;
	v = mrb_hash_lookup2(opt, sym_encoding, Qnil);
	if (v != Qnil) encoding = v;
	v = mrb_hash_lookup2(opt, sym_extenc, Qundef);
	if (v != Qnil) extenc = v;
	v = mrb_hash_lookup2(opt, sym_intenc, Qundef);
	if (v != Qundef) intenc = v;
    }
    if ((extenc != Qundef || intenc != Qundef) && !NIL_P(encoding)) {
	if (!NIL_P(ruby_verbose)) {
	    int idx = mrb_to_encoding_index(encoding);
	    mrb_warn("Ignoring encoding parameter '%s': %s_encoding is used",
		    idx < 0 ? StringValueCStr(encoding) : mrb_enc_name(mrb_enc_from_index(idx)),
		    extenc == Qundef ? "internal" : "external");
	}
	encoding = Qnil;
    }
    if (extenc != Qundef && !NIL_P(extenc)) {
	extencoding = mrb_to_encoding(extenc);
    }
    if (intenc != Qundef) {
	if (NIL_P(intenc)) {
	    /* internal_encoding: nil => no transcoding */
	    intencoding = (mrb_encoding *)Qnil;
	}
	else if (!NIL_P(tmp = mrb_check_string_type(intenc))) {
	    char *p = StringValueCStr(tmp);

	    if (*p == '-' && *(p+1) == '\0') {
		/* Special case - "-" => no transcoding */
		intencoding = (mrb_encoding *)Qnil;
	    }
	    else {
		intencoding = mrb_to_encoding(intenc);
	    }
	}
	else {
	    intencoding = mrb_to_encoding(intenc);
	}
	if (extencoding == intencoding) {
	    intencoding = (mrb_encoding *)Qnil;
	}
    }
    if (!NIL_P(encoding)) {
	extracted = 1;
	if (!NIL_P(tmp = mrb_check_string_type(encoding))) {
	    parse_mode_enc(StringValueCStr(tmp), enc_p, enc2_p, fmode_p);
	}
	else {
	    mrb_io_ext_int_to_encs(mrb_to_encoding(encoding), NULL, enc_p, enc2_p);
	}
    }
    else if (extenc != Qundef || intenc != Qundef) {
        extracted = 1;
	mrb_io_ext_int_to_encs(mrb_state *mrb, extencoding, intencoding, enc_p, enc2_p);
    }
    return extracted;
}

typedef struct mrb_io_enc_t convconfig_t;

static void
validate_enc_binmode(mrb_state *mrb, int *fmode_p, int ecflags, mrb_encoding *enc, mrb_encoding *enc2)
{
    int fmode = *fmode_p;

    if ((fmode & FMODE_READABLE) &&
        !enc2 &&
!(mrb_state *mrb, fmode & FMODE_BINMODE) &&
!mrb_enc_asciicompat(mrb_state *mrb, enc ? enc : mrb_default_external_encoding()))
mrb_raise(mrb_state *mrb, mrb_eArgError, "ASCII incompatible encoding needs binmode");

    if (!(fmode & FMODE_BINMODE) &&
	(mrb_state *mrb, DEFAULT_TEXTMODE || (ecflags & ECONV_NEWLINE_DECORATOR_MASK))) {
	fmode |= DEFAULT_TEXTMODE;
	*fmode_p = fmode;
    }
#if !DEFAULT_TEXTMODE
    else if (!(ecflags & ECONV_NEWLINE_DECORATOR_MASK)) {
	fmode &= ~FMODE_TEXTMODE;
	*fmode_p = fmode;
    }
#endif
}

static void
extract_binmode(mrb_state *mrb, mrb_value opthash, int *fmode)
{
    if (!NIL_P(opthash)) {
	mrb_value v;
	v = mrb_hash_aref(opthash, sym_textmode);
	if (!NIL_P(v) && RTEST(v))
            *fmode |= FMODE_TEXTMODE;
	v = mrb_hash_aref(opthash, sym_binmode);
	if (!NIL_P(v) && RTEST(v))
            *fmode |= FMODE_BINMODE;

	if ((*fmode & FMODE_BINMODE) && (*fmode & FMODE_TEXTMODE))
	    mrb_raise(mrb_eArgError, "both textmode and binmode specified");
    }
}

static void
mrb_io_extract_modeenc(mrb_state *mrb, mrb_value *vmode_p, mrb_value *vperm_p, mrb_value opthash,
        int *oflags_p, int *fmode_p, convconfig_t *convconfig_p)
{
    mrb_value vmode;
    int oflags, fmode;
    mrb_encoding *enc, *enc2;
    int ecflags;
    mrb_value ecopts;
    int has_enc = 0, has_vmode = 0;
    mrb_value intmode;

    vmode = *vmode_p;

    /* Set to defaults */
mrb_io_ext_int_to_encs(mrb_state *mrb, NULL, NULL, &enc, &enc2);

  vmode_handle:
    if (NIL_P(vmode)) {
        fmode = FMODE_READABLE;
        oflags = O_RDONLY;
    }
    else if (!NIL_P(intmode = mrb_check_to_integer(vmode, "to_int"))) {
        vmode = intmode;
        oflags = NUM2INT(intmode);
        fmode = mrb_io_oflags_fmode(oflags);
    }
    else {
        const char *p;

SafeStringValue(mrb_state *mrb, vmode);
        p = StringValueCStr(vmode);
        fmode = mrb_io_modestr_fmode(p);
        oflags = mrb_io_fmode_oflags(fmode);
        p = strchr(p, ':');
        if (p) {
            has_enc = 1;
parse_mode_enc(mrb_state *mrb, p+1, &enc, &enc2, &fmode);
        }
	else {
	    mrb_encoding *e;

	    e = (fmode & FMODE_BINMODE) ? mrb_ascii8bit_encoding() : NULL;
	    mrb_io_ext_int_to_encs(e, NULL, &enc, &enc2);
	}
    }

    if (NIL_P(opthash)) {
	ecflags = (fmode & FMODE_READABLE) ?
	    MODE_BTMODE(ECONV_DEFAULT_NEWLINE_DECORATOR,
			0, ECONV_UNIVERSAL_NEWLINE_DECORATOR) : 0;
#ifdef TEXTMODE_NEWLINE_DECORATOR_ON_WRITE
	ecflags |= (fmode & FMODE_WRITABLE) ?
	    MODE_BTMODE(TEXTMODE_NEWLINE_DECORATOR_ON_WRITE,
			0, TEXTMODE_NEWLINE_DECORATOR_ON_WRITE) : 0;
#endif
	SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(mrb_state *mrb, enc2, ecflags);
        ecopts = Qnil;
    }
    else {
	mrb_value v;
	extract_binmode(mrb_state *mrb, opthash, &fmode);
#ifdef O_BINARY
	if (fmode & FMODE_BINMODE)
            oflags |= O_BINARY;
#endif
#if DEFAULT_TEXTMODE
	else if (NIL_P(vmode)) {
	    fmode |= DEFAULT_TEXTMODE;
	}
#endif
	if (!has_vmode) {
	    v = mrb_hash_aref(opthash, sym_mode);
	    if (!NIL_P(v)) {
		if (!NIL_P(vmode)) {
		    mrb_raise(mrb_eArgError, "mode specified twice");
		}
		has_vmode = 1;
		vmode = v;
		goto vmode_handle;
	    }
	}
	v = mrb_hash_aref(opthash, sym_perm);
	if (!NIL_P(v)) {
	    if (vperm_p) {
		if (!NIL_P(*vperm_p)) {
		    mrb_raise(mrb_eArgError, "perm specified twice");
		}
		*vperm_p = v;
	    }
	    else {
		/* perm no use, just ignore */
	    }
	}
	ecflags = (fmode & FMODE_READABLE) ?
	    MODE_BTMODE(ECONV_DEFAULT_NEWLINE_DECORATOR,
			0, ECONV_UNIVERSAL_NEWLINE_DECORATOR) : 0;
#ifdef TEXTMODE_NEWLINE_DECORATOR_ON_WRITE
	ecflags |= (fmode & FMODE_WRITABLE) ?
	    MODE_BTMODE(TEXTMODE_NEWLINE_DECORATOR_ON_WRITE,
			0, TEXTMODE_NEWLINE_DECORATOR_ON_WRITE) : 0;
#endif

        if (mrb_io_extract_encoding_option(opthash, &enc, &enc2, &fmode)) {
            if (has_enc) {
mrb_raise(mrb_state *mrb, mrb_eArgError, "encoding specified twice");
            }
        }
	SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(mrb_state *mrb, enc2, ecflags);
	ecflags = mrb_econv_prepare_options(opthash, &ecopts, ecflags);
    }

validate_enc_binmode(mrb_state *mrb, &fmode, ecflags, enc, enc2);

    *vmode_p = vmode;

    *oflags_p = oflags;
    *fmode_p = fmode;
    convconfig_p->enc = enc;
    convconfig_p->enc2 = enc2;
    convconfig_p->ecflags = ecflags;
    convconfig_p->ecopts = ecopts;
}

struct sysopen_struct {
    mrb_value fname;
    int oflags;
    mode_t perm;
};

static mrb_value
sysopen_func(mrb_state *mrb, void *ptr)
{
    const struct sysopen_struct *data = ptr;
    const char *fname = RSTRING_PTR(data->fname);
    return (mrb_value)open(fname, data->oflags, data->perm);
}

static inline int
mrb_sysopen_internal(mrb_state *mrb, struct sysopen_struct *data)
{
    int fd;
    fd = (int)mrb_thread_blocking_region(sysopen_func, data, RUBY_UBF_IO, 0);
    if (0 <= fd)
mrb_update_max_fd(mrb_state *mrb, fd);
    return fd;
}

static int
mrb_sysopen(mrb_state *mrb, mrb_value fname, int oflags, mode_t perm)
{
    int fd;
    struct sysopen_struct data;

    data.fname = mrb_str_encode_ospath(fname);
    data.oflags = oflags;
    data.perm = perm;

    fd = mrb_sysopen_internal(&data);
    if (fd < 0) {
	if (errno == EMFILE || errno == ENFILE) {
	    mrb_gc();
	    fd = mrb_sysopen_internal(&data);
	}
	if (fd < 0) {
	    mrb_sys_fail_path(fname);
	}
    }
mrb_update_max_fd(mrb_state *mrb, fd);
    return fd;
}

FILE *
mrb_fdopen(mrb_state *mrb, int fd, const char *modestr)
{
    FILE *file;

#if defined(sun)
    errno = 0;
#endif
    file = fdopen(fd, modestr);
    if (!file) {
	if (
#if defined(sun)
	    errno == 0 ||
#endif
	    errno == EMFILE || errno == ENFILE) {
	    mrb_gc();
#if defined(sun)
	    errno = 0;
#endif
	    file = fdopen(fd, modestr);
	}
	if (!file) {
#ifdef _WIN32
	    if (errno == 0) errno = EINVAL;
#elif defined(sun)
	    if (errno == 0) errno = EMFILE;
#endif
	    mrb_sys_fail(0);
	}
    }

    /* xxx: should be _IONBF?  A buffer in FILE may have trouble. */
#ifdef USE_SETVBUF
    if (setvbuf(file, NULL, _IOFBF, 0) != 0)
	mrb_warn(mrb_state *mrb, "setvbuf() can't be honoured (fd=%d)", fd);
#endif
    return file;
}

static void
io_check_tty(mrb_state *mrb, mrb_io_t *fptr)
{
    if (isatty(fptr->fd))
        fptr->mode |= FMODE_TTY|FMODE_DUPLEX;
}

static mrb_value mrb_io_internal_encoding(mrb_value);
static void io_encoding_set(mrb_io_t *, mrb_value, mrb_value, mrb_value);

static int
io_strip_bom(mrb_state *mrb, mrb_value io)
{
    mrb_value b1, b2, b3, b4;

    if (NIL_P(b1 = mrb_io_getbyte(io))) return 0;
    switch (b1) {
      case INT2FIX(0xEF):
	if (NIL_P(b2 = mrb_io_getbyte(io))) break;
	if (b2 == INT2FIX(0xBB) && !NIL_P(b3 = mrb_io_getbyte(io))) {
	    if (b3 == INT2FIX(0xBF)) {
		return mrb_utf8_encindex();
	    }
	    mrb_io_ungetbyte(io, b3);
	}
	mrb_io_ungetbyte(mrb_state *mrb, io, b2);
	break;

      case INT2FIX(0xFE):
	if (NIL_P(b2 = mrb_io_getbyte(io))) break;
	if (b2 == INT2FIX(0xFF)) {
	    return mrb_enc_find_index("UTF-16BE");
	}
	mrb_io_ungetbyte(mrb_state *mrb, io, b2);
	break;

      case INT2FIX(0xFF):
	if (NIL_P(b2 = mrb_io_getbyte(io))) break;
	if (b2 == INT2FIX(0xFE)) {
	    b3 = mrb_io_getbyte(io);
	    if (b3 == INT2FIX(0) && !NIL_P(b4 = mrb_io_getbyte(io))) {
		if (b4 == INT2FIX(0)) {
		    return mrb_enc_find_index("UTF-32LE");
		}
		mrb_io_ungetbyte(mrb_state *mrb, io, b4);
		mrb_io_ungetbyte(mrb_state *mrb, io, b3);
	    }
	    else {
		mrb_io_ungetbyte(mrb_state *mrb, io, b3);
		return mrb_enc_find_index("UTF-16LE");
	    }
	}
	mrb_io_ungetbyte(mrb_state *mrb, io, b2);
	break;

      case INT2FIX(0):
	if (NIL_P(b2 = mrb_io_getbyte(io))) break;
	if (b2 == INT2FIX(0) && !NIL_P(b3 = mrb_io_getbyte(io))) {
	    if (b3 == INT2FIX(0xFE) && !NIL_P(b4 = mrb_io_getbyte(io))) {
		if (b4 == INT2FIX(0xFF)) {
		    return mrb_enc_find_index("UTF-32BE");
		}
		mrb_io_ungetbyte(mrb_state *mrb, io, b4);
	    }
	    mrb_io_ungetbyte(io, b3);
	}
	mrb_io_ungetbyte(mrb_state *mrb, io, b2);
	break;
    }
mrb_io_ungetbyte(mrb_state *mrb, io, b1);
    return 0;
}

static void
io_set_encoding_by_bom(mrb_state *mrb, mrb_value io)
{
    int idx = io_strip_bom(io);

    if (idx) {
	mrb_io_t *fptr;
	GetOpenFile(mrb_state *mrb, io, fptr);
	io_encoding_set(mrb_state *mrb, fptr, mrb_enc_from_encoding(mrb_enc_from_index(idx)),
		mrb_io_internal_encoding(mrb_state *mrb, io), Qnil);
    }
}

static mrb_value
mrb_file_open_generic(mrb_state *mrb, mrb_value io, mrb_value filename, int oflags, int fmode, convconfig_t *convconfig, mode_t perm)
{
    mrb_io_t *fptr;
    convconfig_t cc;
    if (!convconfig) {
	/* Set to default encodings */
	mrb_io_ext_int_to_encs(mrb_state *mrb, NULL, NULL, &cc.enc, &cc.enc2);
        cc.ecflags = 0;
        cc.ecopts = Qnil;
        convconfig = &cc;
    }
validate_enc_binmode(mrb_state *mrb, &fmode, convconfig->ecflags,
			 convconfig->enc, convconfig->enc2);

MakeOpenFile(mrb_state *mrb, io, fptr);
    fptr->mode = fmode;
    fptr->encs = *convconfig;
    fptr->pathv = mrb_str_new_frozen(filename);
    fptr->fd = mrb_sysopen(fptr->pathv, oflags, perm);
io_check_tty(mrb_state *mrb, fptr);
    if (fmode & FMODE_SETENC_BY_BOM) io_set_encoding_by_bom(io);

    return io;
}

static mrb_value
mrb_file_open_internal(mrb_state *mrb, mrb_value io, mrb_value filename, const char *modestr)
{
    int fmode = mrb_io_modestr_fmode(modestr);
    const char *p = strchr(modestr, ':');
    convconfig_t convconfig;

    if (p) {
parse_mode_enc(mrb_state *mrb, p+1, &convconfig.enc, &convconfig.enc2, &fmode);
    }
    else {
	mrb_encoding *e;
	/* Set to default encodings */

	e = (fmode & FMODE_BINMODE) ? mrb_ascii8bit_encoding() : NULL;
	mrb_io_ext_int_to_encs(mrb_state *mrb, e, NULL, &convconfig.enc, &convconfig.enc2);
        convconfig.ecflags = 0;
        convconfig.ecopts = Qnil;
    }

    return mrb_file_open_generic(io, filename,
mrb_io_fmode_oflags(mrb_state *mrb, fmode),
            fmode,
            &convconfig,
            0666);
}

mrb_value
mrb_file_open_str(mrb_state *mrb, mrb_value fname, const char *modestr)
{
FilePathValue(mrb_state *mrb, fname);
    return mrb_file_open_internal(io_alloc(mrb_cFile), fname, modestr);
}

mrb_value
mrb_file_open(mrb_state *mrb, const char *fname, const char *modestr)
{
    return mrb_file_open_internal(io_alloc(mrb_cFile), mrb_str_new_cstr(fname), modestr);
}

#if defined(__CYGWIN__) || !defined(HAVE_FORK)
static struct pipe_list {
    mrb_io_t *fptr;
    struct pipe_list *next;
} *pipe_list;

static void
pipe_add_fptr(mrb_state *mrb, mrb_io_t *fptr)
{
    struct pipe_list *list;

    list = ALLOC(struct pipe_list);
    list->fptr = fptr;
    list->next = pipe_list;
    pipe_list = list;
}

static void
pipe_del_fptr(mrb_state *mrb, mrb_io_t *fptr)
{
    struct pipe_list *list = pipe_list;
    struct pipe_list *tmp;

    if (list->fptr == fptr) {
	pipe_list = list->next;
	free(mrb_state *mrb, list);
	return;
    }

    while (list->next) {
	if (list->next->fptr == fptr) {
	    tmp = list->next;
	    list->next = list->next->next;
	    free(tmp);
	    return;
	}
	list = list->next;
    }
}

static void
pipe_atexit(mrb_state *mrb, void)
{
    struct pipe_list *list = pipe_list;
    struct pipe_list *tmp;

    while (list) {
	tmp = list->next;
	mrb_io_fptr_finalize(mrb_state *mrb, list->fptr);
	list = tmp;
    }
}

static void
pipe_finalize(mrb_state *mrb, mrb_io_t *fptr, int noraise)
{
#if !defined(HAVE_FORK) && !defined(_WIN32)
    int status = 0;
    if (fptr->stdio_file) {
	status = pclose(fptr->stdio_file);
    }
    fptr->fd = -1;
    fptr->stdio_file = 0;
mrb_last_status_set(mrb_state *mrb, status, fptr->pid);
#else
fptr_finalize(mrb_state *mrb, fptr, noraise);
#endif
pipe_del_fptr(mrb_state *mrb, fptr);
}
#endif

void
mrb_io_synchronized(mrb_state *mrb, mrb_io_t *fptr)
{
mrb_io_check_initialized(mrb_state *mrb, fptr);
    fptr->mode |= FMODE_SYNC;
}

void
mrb_io_unbuffered(mrb_state *mrb, mrb_io_t *fptr)
{
mrb_io_synchronized(mrb_state *mrb, fptr);
}

int
mrb_pipe(mrb_state *mrb, int *pipes)
{
    int ret;
    ret = pipe(pipes);
    if (ret == -1) {
        if (errno == EMFILE || errno == ENFILE) {
mrb_gc(mrb_state *mrb, );
            ret = pipe(pipes);
        }
    }
    if (ret == 0) {
mrb_update_max_fd(mrb_state *mrb, pipes[0]);
mrb_update_max_fd(mrb_state *mrb, pipes[1]);
    }
    return ret;
}

#ifdef HAVE_FORK
struct popen_arg {
    struct mrb_exec_arg *execp;
    int modef;
    int pair[2];
    int write_pair[2];
};

static void
popen_redirect(mrb_state *mrb, struct popen_arg *p)
{
    if ((p->modef & FMODE_READABLE) && (p->modef & FMODE_WRITABLE)) {
close(mrb_state *mrb, p->write_pair[1]);
        if (p->write_pair[0] != 0) {
dup2(mrb_state *mrb, p->write_pair[0], 0);
close(mrb_state *mrb, p->write_pair[0]);
        }
close(mrb_state *mrb, p->pair[0]);
        if (p->pair[1] != 1) {
dup2(mrb_state *mrb, p->pair[1], 1);
close(mrb_state *mrb, p->pair[1]);
        }
    }
    else if (p->modef & FMODE_READABLE) {
close(mrb_state *mrb, p->pair[0]);
        if (p->pair[1] != 1) {
dup2(mrb_state *mrb, p->pair[1], 1);
close(mrb_state *mrb, p->pair[1]);
        }
    }
    else {
close(mrb_state *mrb, p->pair[1]);
        if (p->pair[0] != 0) {
dup2(mrb_state *mrb, p->pair[0], 0);
close(mrb_state *mrb, p->pair[0]);
        }
    }
}

void
mrb_close_before_exec(mrb_state *mrb, int lowfd, int maxhint, mrb_value noclose_fds)
{
    int fd, ret;
    int max = max_file_descriptor;
    if (max < maxhint)
        max = maxhint;
    for (fd = lowfd; fd <= max; fd++) {
        if (!NIL_P(noclose_fds) &&
RTEST(mrb_state *mrb, mrb_hash_lookup(noclose_fds, INT2FIX(fd))))
            continue;
#ifdef FD_CLOEXEC
	ret = fcntl(fd, F_GETFD);
	if (ret != -1 && !(ret & FD_CLOEXEC)) {
fcntl(mrb_state *mrb, fd, F_SETFD, ret|FD_CLOEXEC);
        }
#else
	ret = close(fd);
#endif
#define CONTIGUOUS_CLOSED_FDS 20
        if (ret != -1) {
	    if (max < fd + CONTIGUOUS_CLOSED_FDS)
		max = fd + CONTIGUOUS_CLOSED_FDS;
	}
    }
}

static int
popen_exec(mrb_state *mrb, void *pp, char *errmsg, size_t errmsg_len)
{
    struct popen_arg *p = (struct popen_arg*)pp;

mrb_thread_atfork_before_exec(mrb_state *mrb, );
    return mrb_exec_err(p->execp, errmsg, errmsg_len);
}
#endif

static mrb_value
pipe_open(mrb_state *mrb, struct mrb_exec_arg *eargp, mrb_value prog, const char *modestr, int fmode, convconfig_t *convconfig)
{
    mrb_pid_t pid = 0;
    mrb_io_t *fptr;
    mrb_value port;
    mrb_io_t *write_fptr;
    mrb_value write_port;
#if defined(HAVE_FORK)
    int status;
    struct popen_arg arg;
    char errmsg[80] = { '\0' };
#elif defined(_WIN32)
    volatile mrb_value argbuf;
    char **args = NULL;
    int pair[2], write_pair[2];
#endif
#if !defined(HAVE_FORK)
    struct mrb_exec_arg sarg;
#endif
    FILE *fp = 0;
    int fd = -1;
    int write_fd = -1;
    const char *cmd = 0;
    int argc;
    mrb_value *argv;

    if (prog)
        cmd = StringValueCStr(prog);

    if (!eargp) {
        /* fork : IO.popen("-") */
        argc = 0;
        argv = 0;
    }
    else if (eargp->argc) {
        /* no shell : IO.popen([prog, arg0], arg1, ...) */
        argc = eargp->argc;
        argv = eargp->argv;
    }
    else {
        /* with shell : IO.popen(prog) */
        argc = 0;
        argv = 0;
    }

#if defined(HAVE_FORK)
    arg.execp = eargp;
    arg.modef = fmode;
    arg.pair[0] = arg.pair[1] = -1;
    arg.write_pair[0] = arg.write_pair[1] = -1;
    switch (fmode & (FMODE_READABLE|FMODE_WRITABLE)) {
      case FMODE_READABLE|FMODE_WRITABLE:
        if (mrb_pipe(arg.write_pair) < 0)
mrb_sys_fail(mrb_state *mrb, cmd);
        if (mrb_pipe(arg.pair) < 0) {
            int e = errno;
close(mrb_state *mrb, arg.write_pair[0]);
close(mrb_state *mrb, arg.write_pair[1]);
            errno = e;
mrb_sys_fail(mrb_state *mrb, cmd);
        }
        if (eargp) {
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(0), INT2FIX(arg.write_pair[0]));
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(1), INT2FIX(arg.pair[1]));
        }
	break;
      case FMODE_READABLE:
        if (mrb_pipe(arg.pair) < 0)
mrb_sys_fail(mrb_state *mrb, cmd);
        if (eargp)
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(1), INT2FIX(arg.pair[1]));
	break;
      case FMODE_WRITABLE:
        if (mrb_pipe(arg.pair) < 0)
mrb_sys_fail(mrb_state *mrb, cmd);
        if (eargp)
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(0), INT2FIX(arg.pair[0]));
	break;
      default:
mrb_sys_fail(mrb_state *mrb, cmd);
    }
    if (eargp) {
mrb_exec_arg_fixup(mrb_state *mrb, arg.execp);
	pid = mrb_fork_err(&status, popen_exec, &arg, arg.execp->redirect_fds, errmsg, sizeof(errmsg));
    }
    else {
	fflush(mrb_state *mrb, stdin);		/* is it really needed? */
	pid = mrb_fork(&status, 0, 0, Qnil);
	if (pid == 0) {		/* child */
	    mrb_thread_atfork();
	    popen_redirect(&arg);
	    mrb_io_synchronized(RFILE(orig_stdout)->fptr);
	    mrb_io_synchronized(RFILE(orig_stderr)->fptr);
	    return Qnil;
	}
    }

    /* parent */
    if (pid == -1) {
	int e = errno;
	close(mrb_state *mrb, arg.pair[0]);
	close(mrb_state *mrb, arg.pair[1]);
        if ((fmode & (FMODE_READABLE|FMODE_WRITABLE)) == (FMODE_READABLE|FMODE_WRITABLE)) {
close(mrb_state *mrb, arg.write_pair[0]);
close(mrb_state *mrb, arg.write_pair[1]);
        }
	errno = e;
        if (errmsg[0])
mrb_sys_fail(mrb_state *mrb, errmsg);
	mrb_sys_fail(mrb_state *mrb, cmd);
    }
    if ((fmode & FMODE_READABLE) && (fmode & FMODE_WRITABLE)) {
close(mrb_state *mrb, arg.pair[1]);
        fd = arg.pair[0];
close(mrb_state *mrb, arg.write_pair[0]);
        write_fd = arg.write_pair[1];
    }
    else if (fmode & FMODE_READABLE) {
close(mrb_state *mrb, arg.pair[1]);
        fd = arg.pair[0];
    }
    else {
close(mrb_state *mrb, arg.pair[0]);
        fd = arg.pair[1];
    }
#elif defined(_WIN32)
    if (argc) {
	int i;

	if (argc >= (int)(FIXNUM_MAX / sizeof(char *))) {
	    mrb_raise(mrb_eArgError, "too many arguments");
	}
	argbuf = mrb_str_tmp_new((argc+1) * sizeof(char *));
	args = (void *)RSTRING_PTR(argbuf);
	for (i = 0; i < argc; ++i) {
	    args[i] = StringValueCStr(argv[i]);
	}
	args[i] = NULL;
    }
    switch (fmode & (FMODE_READABLE|FMODE_WRITABLE)) {
      case FMODE_READABLE|FMODE_WRITABLE:
        if (mrb_pipe(write_pair) < 0)
mrb_sys_fail(mrb_state *mrb, cmd);
        if (mrb_pipe(pair) < 0) {
            int e = errno;
close(mrb_state *mrb, write_pair[0]);
close(mrb_state *mrb, write_pair[1]);
            errno = e;
mrb_sys_fail(mrb_state *mrb, cmd);
        }
        if (eargp) {
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(0), INT2FIX(write_pair[0]));
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(1), INT2FIX(pair[1]));
        }
	break;
      case FMODE_READABLE:
        if (mrb_pipe(pair) < 0)
mrb_sys_fail(mrb_state *mrb, cmd);
        if (eargp)
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(1), INT2FIX(pair[1]));
	break;
      case FMODE_WRITABLE:
        if (mrb_pipe(pair) < 0)
mrb_sys_fail(mrb_state *mrb, cmd);
        if (eargp)
mrb_exec_arg_addopt(mrb_state *mrb, eargp, INT2FIX(0), INT2FIX(pair[0]));
	break;
      default:
mrb_sys_fail(mrb_state *mrb, cmd);
    }
    if (eargp) {
	mrb_exec_arg_fixup(mrb_state *mrb, eargp);
	mrb_run_exec_options(mrb_state *mrb, eargp, &sarg);
    }
    while ((pid = (args ?
		   mrb_w32_aspawn(P_NOWAIT, cmd, args) :
		   mrb_w32_spawn(P_NOWAIT, cmd, 0))) == -1) {
	/* exec failed */
	switch (errno) {
	  case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
	    mrb_thread_sleep(1);
	    break;
	  default:
	    {
		int e = errno;
		if (eargp)
		    mrb_run_exec_options(&sarg, NULL);
		close(mrb_state *mrb, pair[0]);
		close(mrb_state *mrb, pair[1]);
		if ((fmode & (FMODE_READABLE|FMODE_WRITABLE)) == (FMODE_READABLE|FMODE_WRITABLE)) {
		    close(write_pair[0]);
		    close(write_pair[1]);
		}
		errno = e;
		mrb_sys_fail(mrb_state *mrb, cmd);
	    }
	    break;
	}
    }

RB_GC_GUARD(mrb_state *mrb, argbuf);

    if (eargp)
	mrb_run_exec_options(mrb_state *mrb, &sarg, NULL);
    if ((fmode & FMODE_READABLE) && (fmode & FMODE_WRITABLE)) {
close(mrb_state *mrb, pair[1]);
        fd = pair[0];
close(mrb_state *mrb, write_pair[0]);
        write_fd = write_pair[1];
    }
    else if (fmode & FMODE_READABLE) {
close(mrb_state *mrb, pair[1]);
        fd = pair[0];
    }
    else {
close(mrb_state *mrb, pair[0]);
        fd = pair[1];
    }
#else
    if (argc) {
	prog = mrb_ary_join(mrb_ary_new4(argc, argv), mrb_str_new2(" "));
	cmd = StringValueCStr(prog);
    }
    if (eargp) {
	mrb_exec_arg_fixup(mrb_state *mrb, eargp);
	mrb_run_exec_options(mrb_state *mrb, eargp, &sarg);
    }
    fp = popen(cmd, modestr);
    if (eargp)
	mrb_run_exec_options(mrb_state *mrb, &sarg, NULL);
    if (!fp) mrb_sys_fail_path(prog);
    fd = fileno(fp);
#endif

    port = io_alloc(mrb_cIO);
MakeOpenFile(mrb_state *mrb, port, fptr);
    fptr->fd = fd;
    fptr->stdio_file = fp;
    fptr->mode = fmode | FMODE_SYNC|FMODE_DUPLEX;
    if (convconfig) {
        fptr->encs = *convconfig;
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
	if (fptr->encs.ecflags & ECONV_DEFAULT_NEWLINE_DECORATOR) {
	    fptr->encs.ecflags |= ECONV_UNIVERSAL_NEWLINE_DECORATOR;
	}
#endif
    }
    else {
	if (NEED_NEWLINE_DECORATOR_ON_READ(fptr)) {
	    fptr->encs.ecflags |= ECONV_UNIVERSAL_NEWLINE_DECORATOR;
	}
#ifdef TEXTMODE_NEWLINE_DECORATOR_ON_WRITE
	if (NEED_NEWLINE_DECORATOR_ON_WRITE(fptr)) {
	    fptr->encs.ecflags |= TEXTMODE_NEWLINE_DECORATOR_ON_WRITE;
	}
#endif
    }
    fptr->pid = pid;

    if (0 <= write_fd) {
        write_port = io_alloc(mrb_cIO);
MakeOpenFile(mrb_state *mrb, write_port, write_fptr);
        write_fptr->fd = write_fd;
        write_fptr->mode = (fmode & ~FMODE_READABLE)| FMODE_SYNC|FMODE_DUPLEX;
        fptr->mode &= ~FMODE_WRITABLE;
        fptr->tied_io_for_writing = write_port;
mrb_ivar_set(mrb_state *mrb, port, mrb_intern("@tied_io_for_writing"), write_port);
    }

#if defined (__CYGWIN__) || !defined(HAVE_FORK)
    fptr->finalize = pipe_finalize;
pipe_add_fptr(mrb_state *mrb, fptr);
#endif
    return port;
}

static mrb_value
pipe_open_v(mrb_state *mrb, int argc, mrb_value *argv, const char *modestr, int fmode, convconfig_t *convconfig)
{
    mrb_value prog;
    struct mrb_exec_arg earg;
    prog = mrb_exec_arg_init(argc, argv, FALSE, &earg);
    return pipe_open(&earg, prog, modestr, fmode, convconfig);
}

static mrb_value
pipe_open_s(mrb_state *mrb, mrb_value prog, const char *modestr, int fmode, convconfig_t *convconfig)
{
    const char *cmd = RSTRING_PTR(prog);
    int argc = 1;
    mrb_value *argv = &prog;
    struct mrb_exec_arg earg;

    if (RSTRING_LEN(prog) == 1 && cmd[0] == '-') {
#if !defined(HAVE_FORK)
	mrb_raise(mrb_state *mrb, mrb_eNotImpError,
		 "fork() function is unimplemented on this machine");
#endif
        return pipe_open(0, 0, modestr, fmode, convconfig);
    }

mrb_exec_arg_init(mrb_state *mrb, argc, argv, TRUE, &earg);
    return pipe_open(&earg, prog, modestr, fmode, convconfig);
}

/*
 *  call-seq:
 *     IO.popen(cmd, mode="r" [, opt])               -> io
 *     IO.popen(cmd, mode="r" [, opt]) {|io| block } -> obj
 *
 *  Runs the specified command as a subprocess; the subprocess's
 *  standard input and output will be connected to the returned
 *  <code>IO</code> object.
 *
 *  The PID of the started process can be obtained by IO#pid method.
 *
 *  _cmd_ is a string or an array as follows.
 *
 *    cmd:
 *      "-"                                      : fork
 *      commandline                              : command line string which is passed to a shell
 *      [env, cmdname, arg1, ..., opts]          : command name and zero or more arguments (no shell)
 *      [env, [cmdname, argv0], arg1, ..., opts] : command name, argv[0] and zero or more arguments (no shell)
 *    (env and opts are optional.)
 *
 *  If _cmd_ is a +String+ ``<code>-</code>'',
 *  then a new instance of Ruby is started as the subprocess.
 *
 *  If <i>cmd</i> is an +Array+ of +String+,
 *  then it will be used as the subprocess's +argv+ bypassing a shell.
 *  The array can contains a hash at first for environments and
 *  a hash at last for options similar to <code>spawn</code>.
 *
 *  The default mode for the new file object is ``r'',
 *  but <i>mode</i> may be set to any of the modes listed in the description for class IO.
 *  The last argument <i>opt</i> qualifies <i>mode</i>.
 *
 *    # set IO encoding
 *    IO.popen("nkf -e filename", :external_encoding=>"EUC-JP") {|nkf_io|
 *      euc_jp_string = nkf_io.read
 *    }
 *
 *    # merge standard output and standard error using
 *    # spawn option.  See the document of Kernel.spawn.
 *    IO.popen(["ls", "/", :err=>[:child, :out]]) {|ls_io|
 *      ls_result_with_error = ls_io.read
 *    }
 *
 *  Raises exceptions which <code>IO.pipe</code> and
 *  <code>Kernel.spawn</code> raise.
 *
 *  If a block is given, Ruby will run the command as a child connected
 *  to Ruby with a pipe. Ruby's end of the pipe will be passed as a
 *  parameter to the block.
 *  At the end of block, Ruby close the pipe and sets <code>$?</code>.
 *  In this case <code>IO.popen</code> returns
 *  the value of the block.
 *
 *  If a block is given with a _cmd_ of ``<code>-</code>'',
 *  the block will be run in two separate processes: once in the parent,
 *  and once in a child. The parent process will be passed the pipe
 *  object as a parameter to the block, the child version of the block
 *  will be passed <code>nil</code>, and the child's standard in and
 *  standard out will be connected to the parent through the pipe. Not
 *  available on all platforms.
 *
 *     f = IO.popen("uname")
 *     p f.readlines
 *     f.close
 *     puts "Parent is #{Process.pid}"
 *     IO.popen("date") { |f| puts f.gets }
 *     IO.popen("-") {|f| $stderr.puts "#{Process.pid} is here, f is #{f.inspect}"}
 *     p $?
 *     IO.popen(%w"sed -e s|^|<foo>| -e s&$&;zot;&", "r+") {|f|
 *       f.puts "bar"; f.close_write; puts f.gets
 *     }
 *
 *  <em>produces:</em>
 *
 *     ["Linux\n"]
 *     Parent is 21346
 *     Thu Jan 15 22:41:19 JST 2009
 *     21346 is here, f is #<IO:fd 3>
 *     21352 is here, f is nil
 *     #<Process::Status: pid 21352 exit 0>
 *     <foo>bar;zot;
 */

static mrb_value
mrb_io_s_popen(mrb_state *mrb, int argc, mrb_value *argv, mrb_value klass)
{
    const char *modestr;
    mrb_value pname, pmode, port, tmp, opt;
    int oflags, fmode;
    convconfig_t convconfig;

    argc = mrb_scan_args(argc, argv, "11:", &pname, &pmode, &opt);

mrb_io_extract_modeenc(mrb_state *mrb, &pmode, 0, opt, &oflags, &fmode, &convconfig);
    modestr = mrb_io_oflags_modestr(oflags);

    tmp = mrb_check_array_type(pname);
    if (!NIL_P(tmp)) {
	long len = RARRAY_LEN(tmp);
#if SIZEOF_LONG > SIZEOF_INT
	if (len > INT_MAX) {
	    mrb_raise(mrb_eArgError, "too many arguments");
	}
#endif
	tmp = mrb_ary_dup(tmp);
	RBASIC(mrb_state *mrb, tmp)->klass = 0;
	port = pipe_open_v((int)len, RARRAY_PTR(tmp), modestr, fmode, &convconfig);
	mrb_ary_clear(mrb_state *mrb, tmp);
    }
    else {
	SafeStringValue(mrb_state *mrb, pname);
	port = pipe_open_s(pname, modestr, fmode, &convconfig);
    }
    if (NIL_P(port)) {
	/* child */
	if (mrb_block_given_p()) {
	    mrb_yield(Qnil);
mrb_io_flush(mrb_state *mrb, mrb_stdout);
mrb_io_flush(mrb_state *mrb, mrb_stderr);
	    _exit(0);
	}
	return Qnil;
    }
RBASIC(mrb_state *mrb, port)->klass = klass;
    if (mrb_block_given_p()) {
	return mrb_ensure(mrb_yield, port, io_close, port);
    }
    return port;
}

static void
mrb_scan_open_args(mrb_state *mrb, int argc, mrb_value *argv,
        mrb_value *fname_p, int *oflags_p, int *fmode_p,
        convconfig_t *convconfig_p, mode_t *perm_p)
{
    mrb_value opt, fname, vmode, vperm;
    int oflags, fmode;
    mode_t perm;

    argc = mrb_scan_args(argc, argv, "12:", &fname, &vmode, &vperm, &opt);
FilePathValue(mrb_state *mrb, fname);

mrb_io_extract_modeenc(mrb_state *mrb, &vmode, &vperm, opt, &oflags, &fmode, convconfig_p);

    perm = NIL_P(vperm) ? 0666 :  NUM2MODET(vperm);

    *fname_p = fname;
    *oflags_p = oflags;
    *fmode_p = fmode;
    *perm_p = perm;
}

static mrb_value
mrb_open_file(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value fname;
    int oflags, fmode;
    convconfig_t convconfig;
    mode_t perm;

mrb_scan_open_args(mrb_state *mrb, argc, argv, &fname, &oflags, &fmode, &convconfig, &perm);
mrb_file_open_generic(mrb_state *mrb, io, fname, oflags, fmode, &convconfig, perm);

    return io;
}


/*
 *  Document-method: File::open
 *
 *  call-seq:
 *     File.open(filename, mode="r" [, opt])                 -> file
 *     File.open(filename [, mode [, perm]] [, opt])         -> file
 *     File.open(filename, mode="r" [, opt]) {|file| block } -> obj
 *     File.open(filename [, mode [, perm]] [, opt]) {|file| block } -> obj
 *
 *  With no associated block, <code>File.open</code> is a synonym for
 *  File.new. If the optional code block is given, it will
 *  be passed the opened +file+ as an argument, and the File object will
 *  automatically be closed when the block terminates.  In this instance,
 *  <code>File.open</code> returns the value of the block.
 *
 *  See IO.new for a list of values for the +opt+ parameter.
 */

/*
 *  Document-method: IO::open
 *
 *  call-seq:
 *     IO.open(fd, mode_string="r" [, opt])               -> io
 *     IO.open(fd, mode_string="r" [, opt]) {|io| block } -> obj
 *
 *  With no associated block, <code>IO.open</code> is a synonym for IO.new. If
 *  the optional code block is given, it will be passed +io+ as an
 *  argument, and the IO object will automatically be closed when the block
 *  terminates. In this instance, IO.open returns the value of the block.
 *
 *  See IO.new for a description of values for the +opt+ parameter.
 *
 */

static mrb_value
mrb_io_s_open(mrb_state *mrb, int argc, mrb_value *argv, mrb_value klass)
{
    mrb_value io = mrb_class_new_instance(argc, argv, klass);

    if (mrb_block_given_p()) {
	return mrb_ensure(mrb_yield, io, io_close, io);
    }

    return io;
}

/*
 *  call-seq:
 *     IO.sysopen(path, [mode, [perm]])  -> fixnum
 *
 *  Opens the given path, returning the underlying file descriptor as a
 *  <code>Fixnum</code>.
 *
 *     IO.sysopen("testfile")   #=> 3
 *
 */

static mrb_value
mrb_io_s_sysopen(mrb_state *mrb, int argc, mrb_value *argv)
{
    mrb_value fname, vmode, vperm;
    mrb_value intmode;
    int oflags, fd;
    mode_t perm;

mrb_scan_args(mrb_state *mrb, argc, argv, "12", &fname, &vmode, &vperm);
FilePathValue(mrb_state *mrb, fname);

    if (NIL_P(vmode))
        oflags = O_RDONLY;
    else if (!NIL_P(intmode = mrb_check_to_integer(vmode, "to_int")))
        oflags = NUM2INT(intmode);
    else {
	SafeStringValue(mrb_state *mrb, vmode);
	oflags = mrb_io_modestr_oflags(StringValueCStr(vmode));
    }
    if (NIL_P(vperm)) perm = 0666;
    else              perm = NUM2MODET(vperm);

RB_GC_GUARD(mrb_state *mrb, fname) = mrb_str_new4(fname);
    fd = mrb_sysopen(fname, oflags, perm);
    return INT2NUM(fd);
}

static mrb_value
check_pipe_command(mrb_state *mrb, mrb_value filename_or_command)
{
    char *s = RSTRING_PTR(filename_or_command);
    long l = RSTRING_LEN(filename_or_command);
    char *e = s + l;
    int chlen;

    if (mrb_enc_ascget(s, e, &chlen, mrb_enc_get(filename_or_command)) == '|') {
        mrb_value cmd = mrb_str_new(s+chlen, l-chlen);
OBJ_INFECT(mrb_state *mrb, cmd, filename_or_command);
        return cmd;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     open(path [, mode_enc [, perm]] [, opt])                -> io or nil
 *     open(path [, mode_enc [, perm]] [, opt]) {|io| block }  -> obj
 *
 *  Creates an <code>IO</code> object connected to the given stream,
 *  file, or subprocess.
 *
 *  If <i>path</i> does not start with a pipe character
 *  (``<code>|</code>''), treat it as the name of a file to open using
 *  the specified mode (defaulting to ``<code>r</code>'').
 *
 *  The mode_enc is
 *  either a string or an integer.  If it is an integer, it must be
 *  bitwise-or of open(2) flags, such as File::RDWR or File::EXCL.
 *  If it is a string, it is either "mode", "mode:ext_enc", or
 *  "mode:ext_enc:int_enc".
 *  The mode is one of the following:
 *
 *   r: read (default)
 *   w: write
 *   a: append
 *
 *  The mode can be followed by "b" (means binary-mode), or "+"
 *  (means both reading and writing allowed) or both.
 *  If ext_enc (external encoding) is specified,
 *  read string will be tagged by the encoding in reading,
 *  and output string will be converted
 *  to the specified encoding in writing.
 *  If ext_enc starts with 'BOM|', check whether the input has a BOM. If
 *  there is a BOM, strip it and set external encoding as
 *  what the BOM tells. If there is no BOM, use ext_enc without 'BOM|'.
 *  If two encoding names,
 *  ext_enc and int_enc (external encoding and internal encoding),
 *  are specified, the read string is converted from ext_enc
 *  to int_enc then tagged with the int_enc in read mode,
 *  and in write mode, the output string will be
 *  converted from int_enc to ext_enc before writing.
 *
 *  If a file is being created, its initial permissions may be
 *  set using the integer third parameter.
 *
 *  If a block is specified, it will be invoked with the
 *  <code>File</code> object as a parameter, and the file will be
 *  automatically closed when the block terminates. The call
 *  returns the value of the block.
 *
 *  If <i>path</i> starts with a pipe character, a subprocess is
 *  created, connected to the caller by a pair of pipes. The returned
 *  <code>IO</code> object may be used to write to the standard input
 *  and read from the standard output of this subprocess. If the command
 *  following the ``<code>|</code>'' is a single minus sign, Ruby forks,
 *  and this subprocess is connected to the parent. In the subprocess,
 *  the <code>open</code> call returns <code>nil</code>. If the command
 *  is not ``<code>-</code>'', the subprocess runs the command. If a
 *  block is associated with an <code>open("|-")</code> call, that block
 *  will be run twice---once in the parent and once in the child. The
 *  block parameter will be an <code>IO</code> object in the parent and
 *  <code>nil</code> in the child. The parent's <code>IO</code> object
 *  will be connected to the child's <code>$stdin</code> and
 *  <code>$stdout</code>. The subprocess will be terminated at the end
 *  of the block.
 *
 *     open("testfile") do |f|
 *       print f.gets
 *     end
 *
 *  <em>produces:</em>
 *
 *     This is line one
 *
 *  Open a subprocess and read its output:
 *
 *     cmd = open("|date")
 *     print cmd.gets
 *     cmd.close
 *
 *  <em>produces:</em>
 *
 *     Wed Apr  9 08:56:31 CDT 2003
 *
 *  Open a subprocess running the same Ruby program:
 *
 *     f = open("|-", "w+")
 *     if f == nil
 *       puts "in Child"
 *       exit
 *     else
 *       puts "Got: #{f.gets}"
 *     end
 *
 *  <em>produces:</em>
 *
 *     Got: in Child
 *
 *  Open a subprocess using a block to receive the I/O object:
 *
 *     open("|-") do |f|
 *       if f == nil
 *         puts "in Child"
 *       else
 *         puts "Got: #{f.gets}"
 *       end
 *     end
 *
 *  <em>produces:</em>
 *
 *     Got: in Child
 */

static mrb_value
mrb_f_open(mrb_state *mrb, int argc, mrb_value *argv)
{
    ID to_open = 0;
    int redirect = FALSE;

    if (argc >= 1) {
	CONST_ID(mrb_state *mrb, to_open, "to_open");
	if (mrb_respond_to(argv[0], to_open)) {
	    redirect = TRUE;
	}
	else {
	    mrb_value tmp = argv[0];
	    FilePathValue(tmp);
	    if (NIL_P(tmp)) {
		redirect = TRUE;
	    }
	    else {
                mrb_value cmd = check_pipe_command(tmp);
                if (!NIL_P(cmd)) {
		    argv[0] = cmd;
		    return mrb_io_s_popen(argc, argv, mrb_cIO);
		}
	    }
	}
    }
    if (redirect) {
	mrb_value io = mrb_funcall2(argv[0], to_open, argc-1, argv+1);

	if (mrb_block_given_p()) {
	    return mrb_ensure(mrb_yield, io, io_close, io);
	}
	return io;
    }
    return mrb_io_s_open(argc, argv, mrb_cFile);
}

static mrb_value
mrb_io_open(mrb_state *mrb, mrb_value filename, mrb_value vmode, mrb_value vperm, mrb_value opt)
{
    mrb_value cmd;
    int oflags, fmode;
    convconfig_t convconfig;
    mode_t perm;

mrb_io_extract_modeenc(mrb_state *mrb, &vmode, &vperm, opt, &oflags, &fmode, &convconfig);
    perm = NIL_P(vperm) ? 0666 :  NUM2MODET(vperm);

    if (!NIL_P(cmd = check_pipe_command(filename))) {
	return pipe_open_s(cmd, mrb_io_oflags_modestr(oflags), fmode, &convconfig);
    }
    else {
        return mrb_file_open_generic(io_alloc(mrb_cFile), filename,
                oflags, fmode, &convconfig, perm);
    }
}

static mrb_value
mrb_io_open_with_args(mrb_state *mrb, int argc, mrb_value *argv)
{
    mrb_value io;

    io = io_alloc(mrb_cFile);
mrb_open_file(mrb_state *mrb, argc, argv, io);
    return io;
}

static mrb_value
io_reopen(mrb_state *mrb, mrb_value io, mrb_value nfile)
{
    mrb_io_t *fptr, *orig;
    int fd, fd2;
    off_t pos = 0;

    nfile = mrb_io_get_io(nfile);
    if (mrb_safe_level() >= 4 &&
	(mrb_state *mrb, !OBJ_UNTRUSTED(io) || !OBJ_UNTRUSTED(nfile))) {
	mrb_raise(mrb_state *mrb, mrb_eSecurityError, "Insecure: can't reopen");
    }
GetOpenFile(mrb_state *mrb, io, fptr);
GetOpenFile(mrb_state *mrb, nfile, orig);

    if (fptr == orig) return io;
    if (IS_PREP_STDIO(fptr)) {
        if ((fptr->stdio_file == stdin && !(orig->mode & FMODE_READABLE)) ||
(mrb_state *mrb, fptr->stdio_file == stdout && !(orig->mode & FMODE_WRITABLE)) ||
(mrb_state *mrb, fptr->stdio_file == stderr && !(orig->mode & FMODE_WRITABLE))) {
	    mrb_raise(mrb_eArgError,
		     "%s can't change access mode from \"%s\" to \"%s\"",
		     PREP_STDIO_NAME(fptr), mrb_io_fmode_modestr(fptr->mode),
		     mrb_io_fmode_modestr(orig->mode));
	}
    }
    if (fptr->mode & FMODE_WRITABLE) {
        if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
    }
    else {
	io_tell(mrb_state *mrb, fptr);
    }
    if (orig->mode & FMODE_READABLE) {
	pos = io_tell(orig);
    }
    if (orig->mode & FMODE_WRITABLE) {
        if (io_fflush(orig) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
    }

    /* copy mrb_io_t structure */
    fptr->mode = orig->mode | (fptr->mode & FMODE_PREP);
    fptr->pid = orig->pid;
    fptr->lineno = orig->lineno;
    if (RTEST(orig->pathv)) fptr->pathv = orig->pathv;
    else if (!IS_PREP_STDIO(fptr)) fptr->pathv = Qnil;
    fptr->finalize = orig->finalize;
#if defined (__CYGWIN__) || !defined(HAVE_FORK)
    if (fptr->finalize == pipe_finalize)
	pipe_add_fptr(mrb_state *mrb, fptr);
#endif

    fd = fptr->fd;
    fd2 = orig->fd;
    if (fd != fd2) {
	if (IS_PREP_STDIO(fptr) || fd <= 2 || !fptr->stdio_file) {
	    /* need to keep FILE objects of stdin, stdout and stderr */
	    if (dup2(fd2, fd) < 0)
		mrb_sys_fail_path(mrb_state *mrb, orig->pathv);
mrb_update_max_fd(mrb_state *mrb, fd);
	}
	else {
fclose(mrb_state *mrb, fptr->stdio_file);
            fptr->stdio_file = 0;
            fptr->fd = -1;
            if (dup2(fd2, fd) < 0)
mrb_sys_fail_path(mrb_state *mrb, orig->pathv);
mrb_update_max_fd(mrb_state *mrb, fd);
            fptr->fd = fd;
	}
	mrb_thread_fd_close(mrb_state *mrb, fd);
	if ((orig->mode & FMODE_READABLE) && pos >= 0) {
	    if (io_seek(fptr, pos, SEEK_SET) < 0 && errno) {
		mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
	    }
	    if (io_seek(orig, pos, SEEK_SET) < 0 && errno) {
		mrb_sys_fail_path(mrb_state *mrb, orig->pathv);
	    }
	}
    }

    if (fptr->mode & FMODE_BINMODE) {
	mrb_io_binmode(mrb_state *mrb, io);
    }

RBASIC(mrb_state *mrb, io)->klass = mrb_obj_class(nfile);
    return io;
}

/*
 *  call-seq:
 *     ios.reopen(other_IO)         -> ios
 *     ios.reopen(path, mode_str)   -> ios
 *
 *  Reassociates <em>ios</em> with the I/O stream given in
 *  <i>other_IO</i> or to a new stream opened on <i>path</i>. This may
 *  dynamically change the actual class of this stream.
 *
 *     f1 = File.new("testfile")
 *     f2 = File.new("testfile")
 *     f2.readlines[0]   #=> "This is line one\n"
 *     f2.reopen(f1)     #=> #<File:testfile>
 *     f2.readlines[0]   #=> "This is line one\n"
 */

static mrb_value
mrb_io_reopen(mrb_state *mrb, int argc, mrb_value *argv, mrb_value file)
{
    mrb_value fname, nmode;
    int oflags;
    mrb_io_t *fptr;

mrb_secure(mrb_state *mrb, 4);
    if (mrb_scan_args(argc, argv, "11", &fname, &nmode) == 1) {
	mrb_value tmp = mrb_io_check_io(fname);
	if (!NIL_P(tmp)) {
	    return io_reopen(file, tmp);
	}
    }

FilePathValue(mrb_state *mrb, fname);
mrb_io_taint_check(mrb_state *mrb, file);
    fptr = RFILE(file)->fptr;
    if (!fptr) {
	fptr = RFILE(file)->fptr = ALLOC(mrb_io_t);
	MEMZERO(mrb_state *mrb, fptr, mrb_io_t, 1);
    }

    if (!NIL_P(nmode)) {
	int fmode = mrb_io_modestr_fmode(StringValueCStr(nmode));
	if (IS_PREP_STDIO(fptr) &&
(mrb_state *mrb, (fptr->mode & FMODE_READWRITE) & (fmode & FMODE_READWRITE)) !=
(mrb_state *mrb, fptr->mode & FMODE_READWRITE)) {
	    mrb_raise(mrb_eArgError,
		     "%s can't change access mode from \"%s\" to \"%s\"",
		     PREP_STDIO_NAME(fptr), mrb_io_fmode_modestr(fptr->mode),
		     mrb_io_fmode_modestr(fmode));
	}
	fptr->mode = fmode;
	mrb_io_mode_enc(mrb_state *mrb, fptr, StringValueCStr(nmode));
        fptr->encs.ecflags = 0;
        fptr->encs.ecopts = Qnil;
    }

    fptr->pathv = mrb_str_new_frozen(fname);
    oflags = mrb_io_fmode_oflags(fptr->mode);
    if (fptr->fd < 0) {
        fptr->fd = mrb_sysopen(fptr->pathv, oflags, 0666);
	fptr->stdio_file = 0;
	return file;
    }

    if (fptr->mode & FMODE_WRITABLE) {
        if (io_fflush(fptr) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
    }
    fptr->rbuf.off = fptr->rbuf.len = 0;

    if (fptr->stdio_file) {
        if (freopen(RSTRING_PTR(fptr->pathv), mrb_io_oflags_modestr(oflags), fptr->stdio_file) == 0) {
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
        }
        fptr->fd = fileno(fptr->stdio_file);
#ifdef USE_SETVBUF
        if (setvbuf(fptr->stdio_file, NULL, _IOFBF, 0) != 0)
mrb_warn(mrb_state *mrb, "setvbuf() can't be honoured for %s", RSTRING_PTR(fptr->pathv));
#endif
    }
    else {
        if (close(fptr->fd) < 0)
mrb_sys_fail_path(mrb_state *mrb, fptr->pathv);
        fptr->fd = -1;
        fptr->fd = mrb_sysopen(fptr->pathv, oflags, 0666);
    }

    return file;
}

/* :nodoc: */
static mrb_value
mrb_io_init_copy(mrb_state *mrb, mrb_value dest, mrb_value io)
{
    mrb_io_t *fptr, *orig;
    int fd;
    mrb_value write_io;
    off_t pos;

    io = mrb_io_get_io(io);
    if (dest == io) return dest;
GetOpenFile(mrb_state *mrb, io, orig);
MakeOpenFile(mrb_state *mrb, dest, fptr);

mrb_io_flush(mrb_state *mrb, io);

    /* copy mrb_io_t structure */
    fptr->mode = orig->mode & ~FMODE_PREP;
    fptr->encs = orig->encs;
    fptr->pid = orig->pid;
    fptr->lineno = orig->lineno;
    if (!NIL_P(orig->pathv)) fptr->pathv = orig->pathv;
    fptr->finalize = orig->finalize;
#if defined (__CYGWIN__) || !defined(HAVE_FORK)
    if (fptr->finalize == pipe_finalize)
	pipe_add_fptr(mrb_state *mrb, fptr);
#endif

    fd = ruby_dup(orig->fd);
    fptr->fd = fd;
    pos = io_tell(orig);
    if (0 <= pos)
io_seek(mrb_state *mrb, fptr, pos, SEEK_SET);
    if (fptr->mode & FMODE_BINMODE) {
	mrb_io_binmode(mrb_state *mrb, dest);
    }

    write_io = GetWriteIO(io);
    if (io != write_io) {
        write_io = mrb_obj_dup(write_io);
        fptr->tied_io_for_writing = write_io;
mrb_ivar_set(mrb_state *mrb, dest, mrb_intern("@tied_io_for_writing"), write_io);
    }

    return dest;
}

/*
 *  call-seq:
 *     ios.printf(format_string [, obj, ...])   -> nil
 *
 *  Formats and writes to <em>ios</em>, converting parameters under
 *  control of the format string. See <code>Kernel#sprintf</code>
 *  for details.
 */

mrb_value
mrb_io_printf(mrb_state *mrb, int argc, mrb_value *argv, mrb_value out)
{
mrb_io_write(mrb_state *mrb, out, mrb_f_sprintf(argc, argv));
    return Qnil;
}

/*
 *  call-seq:
 *     printf(io, string [, obj ... ])    -> nil
 *     printf(string [, obj ... ])        -> nil
 *
 *  Equivalent to:
 *     io.write(sprintf(string, obj, ...)
 *  or
 *     $stdout.write(sprintf(string, obj, ...)
 */

static mrb_value
mrb_f_printf(mrb_state *mrb, int argc, mrb_value *argv)
{
    mrb_value out;

    if (argc == 0) return Qnil;
    if (TYPE(argv[0]) == T_STRING) {
	out = mrb_stdout;
    }
    else {
	out = argv[0];
	argv++;
	argc--;
    }
mrb_io_write(mrb_state *mrb, out, mrb_f_sprintf(argc, argv));

    return Qnil;
}

/*
 *  call-seq:
 *     ios.print()             -> nil
 *     ios.print(obj, ...)     -> nil
 *
 *  Writes the given object(s) to <em>ios</em>. The stream must be
 *  opened for writing. If the output field separator (<code>$,</code>)
 *  is not <code>nil</code>, it will be inserted between each object.
 *  If the output record separator (<code>$\\</code>)
 *  is not <code>nil</code>, it will be appended to the output. If no
 *  arguments are given, prints <code>$_</code>. Objects that aren't
 *  strings will be converted by calling their <code>to_s</code> method.
 *  With no argument, prints the contents of the variable <code>$_</code>.
 *  Returns <code>nil</code>.
 *
 *     $stdout.print("This is ", 100, " percent.\n")
 *
 *  <em>produces:</em>
 *
 *     This is 100 percent.
 */

mrb_value
mrb_io_print(mrb_state *mrb, int argc, mrb_value *argv, mrb_value out)
{
    int i;
    mrb_value line;

    /* if no argument given, print `$_' */
    if (argc == 0) {
	argc = 1;
	line = mrb_lastline_get();
	argv = &line;
    }
    for (i=0; i<argc; i++) {
	if (!NIL_P(mrb_output_fs) && i>0) {
	    mrb_io_write(out, mrb_output_fs);
	}
	mrb_io_write(mrb_state *mrb, out, argv[i]);
    }
    if (argc > 0 && !NIL_P(mrb_output_rs)) {
	mrb_io_write(mrb_state *mrb, out, mrb_output_rs);
    }

    return Qnil;
}

/*
 *  call-seq:
 *     print(obj, ...)    -> nil
 *
 *  Prints each object in turn to <code>$stdout</code>. If the output
 *  field separator (<code>$,</code>) is not +nil+, its
 *  contents will appear between each field. If the output record
 *  separator (<code>$\\</code>) is not +nil+, it will be
 *  appended to the output. If no arguments are given, prints
 *  <code>$_</code>. Objects that aren't strings will be converted by
 *  calling their <code>to_s</code> method.
 *
 *     print "cat", [1,2,3], 99, "\n"
 *     $, = ", "
 *     $\ = "\n"
 *     print "cat", [1,2,3], 99
 *
 *  <em>produces:</em>
 *
 *     cat12399
 *     cat, 1, 2, 3, 99
 */

static mrb_value
mrb_f_print(mrb_state *mrb, int argc, mrb_value *argv)
{
mrb_io_print(mrb_state *mrb, argc, argv, mrb_stdout);
    return Qnil;
}

/*
 *  call-seq:
 *     ios.putc(obj)    -> obj
 *
 *  If <i>obj</i> is <code>Numeric</code>, write the character whose code is
 *  the least-significant byte of <i>obj</i>, otherwise write the first byte
 *  of the string representation of <i>obj</i> to <em>ios</em>. Note: This
 *  method is not safe for use with multi-byte characters as it will truncate
 *  them.
 *
 *     $stdout.putc "A"
 *     $stdout.putc 65
 *
 *  <em>produces:</em>
 *
 *     AA
 */

static mrb_value
mrb_io_putc(mrb_state *mrb, mrb_value io, mrb_value ch)
{
    mrb_value str;
    if (TYPE(ch) == T_STRING) {
	str = mrb_str_substr(ch, 0, 1);
    }
    else {
	char c = NUM2CHR(ch);
	str = mrb_str_new(&c, 1);
    }
mrb_io_write(mrb_state *mrb, io, str);
    return ch;
}

/*
 *  call-seq:
 *     putc(int)   -> int
 *
 *  Equivalent to:
 *
 *    $stdout.putc(int)
 *
 * Refer to the documentation for IO#putc for important information regarding
 * multi-byte characters.
 */

static mrb_value
mrb_f_putc(mrb_state *mrb, mrb_value recv, mrb_value ch)
{
    if (recv == mrb_stdout) {
	return mrb_io_putc(recv, ch);
    }
    return mrb_funcall2(mrb_stdout, mrb_intern("putc"), 1, &ch);
}


static int
str_end_with_asciichar(mrb_state *mrb, mrb_value str, int c)
{
    long len = RSTRING_LEN(str);
    const char *ptr = RSTRING_PTR(str);
    mrb_encoding *enc = mrb_enc_from_index(ENCODING_GET(str));
    int n;

    if (len == 0) return 0;
    if ((n = mrb_enc_mbminlen(enc)) == 1) {
	return ptr[len - 1] == c;
    }
    return mrb_enc_ascget(ptr + ((len - 1) / n) * n, ptr + len, &n, enc) == c;
}

static mrb_value
io_puts_ary(mrb_state *mrb, mrb_value ary, mrb_value out, int recur)
{
    mrb_value tmp;
    long i;

    if (recur) {
	tmp = mrb_str_new2("[...]");
	mrb_io_puts(mrb_state *mrb, 1, &tmp, out);
	return Qnil;
    }
    for (i=0; i<RARRAY_LEN(ary); i++) {
	tmp = RARRAY_PTR(ary)[i];
	mrb_io_puts(mrb_state *mrb, 1, &tmp, out);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     ios.puts(obj, ...)    -> nil
 *
 *  Writes the given objects to <em>ios</em> as with
 *  <code>IO#print</code>. Writes a record separator (typically a
 *  newline) after any that do not already end with a newline sequence.
 *  If called with an array argument, writes each element on a new line.
 *  If called without arguments, outputs a single record separator.
 *
 *     $stdout.puts("this", "is", "a", "test")
 *
 *  <em>produces:</em>
 *
 *     this
 *     is
 *     a
 *     test
 */

mrb_value
mrb_io_puts(mrb_state *mrb, int argc, mrb_value *argv, mrb_value out)
{
    int i;
    mrb_value line;

    /* if no argument given, print newline. */
    if (argc == 0) {
	mrb_io_write(mrb_state *mrb, out, mrb_default_rs);
	return Qnil;
    }
    for (i=0; i<argc; i++) {
	if (TYPE(argv[i]) == T_STRING) {
	    line = argv[i];
	    goto string;
	}
	line = mrb_check_array_type(argv[i]);
	if (!NIL_P(line)) {
	    mrb_exec_recursive(io_puts_ary, line, out);
	    continue;
	}
	line = mrb_obj_as_string(argv[i]);
      string:
	mrb_io_write(mrb_state *mrb, out, line);
	if (RSTRING_LEN(line) == 0 ||
!str_end_with_asciichar(mrb_state *mrb, line, '\n')) {
	    mrb_io_write(out, mrb_default_rs);
	}
    }

    return Qnil;
}

/*
 *  call-seq:
 *     puts(obj, ...)    -> nil
 *
 *  Equivalent to
 *
 *      $stdout.puts(obj, ...)
 */

static mrb_value
mrb_f_puts(mrb_state *mrb, int argc, mrb_value *argv, mrb_value recv)
{
    if (recv == mrb_stdout) {
	return mrb_io_puts(argc, argv, recv);
    }
    return mrb_funcall2(mrb_stdout, mrb_intern("puts"), argc, argv);
}

void
mrb_p(mrb_state *mrb, mrb_value obj) /* for debug print within C code */
{
    mrb_value str = mrb_obj_as_string(mrb_inspect(obj));
    if (TYPE(mrb_stdout) == T_FILE &&
mrb_method_basic_definition_p(mrb_state *mrb, CLASS_OF(mrb_stdout), id_write)) {
io_write(mrb_state *mrb, mrb_stdout, str, 1);
io_write(mrb_state *mrb, mrb_stdout, mrb_default_rs, 0);
    }
    else {
mrb_io_write(mrb_state *mrb, mrb_stdout, str);
mrb_io_write(mrb_state *mrb, mrb_stdout, mrb_default_rs);
    }
}

/*
 *  call-seq:
 *     p(obj)              -> obj
 *     p(obj1, obj2, ...)  -> [obj, ...]
 *     p()                 -> nil
 *
 *  For each object, directly writes _obj_.+inspect+ followed by a
 *  newline to the program's standard output.
 *
 *     S = Struct.new(:name, :state)
 *     s = S['dave', 'TX']
 *     p s
 *
 *  <em>produces:</em>
 *
 *     #<S name="dave", state="TX">
 */

static mrb_value
mrb_f_p(mrb_state *mrb, int argc, mrb_value *argv, mrb_value self)
{
    int i;
    mrb_value ret = Qnil;

    for (i=0; i<argc; i++) {
	mrb_p(mrb_state *mrb, argv[i]);
    }
    if (argc == 1) {
	ret = argv[0];
    }
    else if (argc > 1) {
	ret = mrb_ary_new4(argc, argv);
    }
    if (TYPE(mrb_stdout) == T_FILE) {
	mrb_io_flush(mrb_state *mrb, mrb_stdout);
    }
    return ret;
}

/*
 *  call-seq:
 *     obj.display(port=$>)    -> nil
 *
 *  Prints <i>obj</i> on the given port (default <code>$></code>).
 *  Equivalent to:
 *
 *     def display(port=$>)
 *       port.write self
 *     end
 *
 *  For example:
 *
 *     1.display
 *     "cat".display
 *     [ 4, 5, 6 ].display
 *     puts
 *
 *  <em>produces:</em>
 *
 *     1cat456
 */

static mrb_value
mrb_obj_display(mrb_state *mrb, int argc, mrb_value *argv, mrb_value self)
{
    mrb_value out;

    if (argc == 0) {
	out = mrb_stdout;
    }
    else {
	mrb_scan_args(mrb_state *mrb, argc, argv, "01", &out);
    }
mrb_io_write(mrb_state *mrb, out, self);

    return Qnil;
}

void
mrb_write_error2(mrb_state *mrb, const char *mesg, long len)
{
    if (mrb_stderr == orig_stderr || RFILE(orig_stderr)->fptr->fd < 0) {
	(mrb_state *mrb, void)fwrite(mesg, sizeof(char), len, stderr);
    }
    else {
	mrb_io_write(mrb_state *mrb, mrb_stderr, mrb_str_new(mesg, len));
    }
}

void
mrb_write_error(mrb_state *mrb, const char *mesg)
{
mrb_write_error2(mrb_state *mrb, mesg, strlen(mesg));
}

static void
must_respond_to(mrb_state *mrb, ID mid, mrb_value val, ID id)
{
    if (!mrb_respond_to(val, mid)) {
	mrb_raise(mrb_state *mrb, mrb_eTypeError, "%s must have %s method, %s given",
		 mrb_id2name(id), mrb_id2name(mid),
		 mrb_obj_classname(val));
    }
}

static void
stdout_setter(mrb_state *mrb, mrb_value val, ID id, mrb_value *variable)
{
must_respond_to(mrb_state *mrb, id_write, val, id);
    *variable = val;
}

static mrb_value
prep_io(mrb_state *mrb, int fd, int fmode, mrb_value klass, const char *path)
{
    mrb_io_t *fp;
    mrb_value io = io_alloc(klass);

MakeOpenFile(mrb_state *mrb, io, fp);
    fp->fd = fd;
#ifdef __CYGWIN__
    if (!isatty(fd)) {
        fmode |= FMODE_BINMODE;
	setmode(mrb_state *mrb, fd, O_BINARY);
    }
#endif
    fp->mode = fmode;
io_check_tty(mrb_state *mrb, fp);
    if (path) fp->pathv = mrb_obj_freeze(mrb_str_new_cstr(path));
mrb_update_max_fd(mrb_state *mrb, fd);

    return io;
}

mrb_value
mrb_io_fdopen(mrb_state *mrb, int fd, int oflags, const char *path)
{
    mrb_value klass = mrb_cIO;

    if (path && strcmp(path, "-")) klass = mrb_cFile;
    return prep_io(fd, mrb_io_oflags_fmode(oflags), klass, path);
}

static mrb_value
prep_stdio(mrb_state *mrb, FILE *f, int fmode, mrb_value klass, const char *path)
{
    mrb_io_t *fptr;
    mrb_value io = prep_io(fileno(f), fmode|FMODE_PREP|DEFAULT_TEXTMODE, klass, path);

GetOpenFile(mrb_state *mrb, io, fptr);
    fptr->encs.ecflags |= ECONV_DEFAULT_NEWLINE_DECORATOR;
#ifdef TEXTMODE_NEWLINE_DECORATOR_ON_WRITE
    fptr->encs.ecflags |= TEXTMODE_NEWLINE_DECORATOR_ON_WRITE;
    if (fmode & FMODE_READABLE) {
	fptr->encs.ecflags |= ECONV_UNIVERSAL_NEWLINE_DECORATOR;
    }
#endif
    fptr->stdio_file = f;

    return io;
}

FILE *
mrb_io_stdio_file(mrb_state *mrb, mrb_io_t *fptr)
{
    if (!fptr->stdio_file) {
        int oflags = mrb_io_fmode_oflags(fptr->mode);
        fptr->stdio_file = mrb_fdopen(fptr->fd, mrb_io_oflags_modestr(oflags));
    }
    return fptr->stdio_file;
}

/*
 *  call-seq:
 *     IO.new(fd [, mode] [, opt])   -> io
 *
 *  Returns a new IO object (a stream) for the given IO object or integer file
 *  descriptor and mode string.  See also IO.sysopen and IO.for_fd.
 *
 *  === Parameters
 *
 *  fd:: numeric file descriptor or IO object
 *  mode:: file mode. a string or an integer
 *  opt:: hash for specifying +mode+ by name.
 *
 *  ==== Mode
 *
 *  When mode is an integer it must be combination of the modes defined in
 *  File::Constants.
 *
 *  When mode is a string it must be in one of the following forms:
 *  - "fmode",
 *  - "fmode:extern",
 *  - "fmode:extern:intern".
 *  <code>extern</code> is the external encoding name for the IO.
 *  <code>intern</code> is the internal encoding.
 *  <code>fmode</code> must be a file open mode string. See the description of
 *  class IO for mode string directives.
 *
 *  When the mode of original IO is read only, the mode cannot be changed to
 *  be writable.  Similarly, the mode cannot be changed from write only to
 *  readable.
 *
 *  When such a change is attempted the error is raised in different locations
 *  according to the platform.
 *
 *  ==== Options
 *  +opt+ can have the following keys
 *  :mode ::
 *    Same as +mode+ parameter
 *  :external_encoding ::
 *    External encoding for the IO.  "-" is a synonym for the default external
 *    encoding.
 *  :internal_encoding ::
 *    Internal encoding for the IO.  "-" is a synonym for the default internal
 *    encoding.
 *
 *    If the value is nil no conversion occurs.
 *  :encoding ::
 *    Specifies external and internal encodings as "extern:intern".
 *  :textmode ::
 *    If the value is truth value, same as "t" in argument +mode+.
 *  :binmode ::
 *    If the value is truth value, same as "b" in argument +mode+.
 *  :autoclose ::
 *    If the value is +false+, the +fd+ will be kept open after this IO
 *    instance gets finalized.
 *
 *  Also +opt+ can have same keys in String#encode for controlling conversion
 *  between the external encoding and the internal encoding.
 *
 *  === Example 1
 *
 *    fd = IO.sysopen("/dev/tty", "w")
 *    a = IO.new(fd,"w")
 *    $stderr.puts "Hello"
 *    a.puts "World"
 *
 *  <em>produces:</em>
 *
 *    Hello
 *    World
 *
 *  === Example 2
 *
 *    require 'fcntl'
 *
 *    fd = STDERR.fcntl(Fcntl::F_DUPFD)
 *    io = IO.new(fd, mode: 'w:UTF-16LE', cr_newline: true)
 *    io.puts "Hello, World!"
 *
 *    fd = STDERR.fcntl(Fcntl::F_DUPFD)
 *    io = IO.new(fd, mode: 'w', cr_newline: true,
 *                external_encoding: Encoding::UTF_16LE)
 *    io.puts "Hello, World!"
 *
 *  Both of above print "Hello, World!" in UTF-16LE to standard error output
 *  with converting EOL generated by <code>puts</code> to CR.
 */

static mrb_value
mrb_io_initialize(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value fnum, vmode;
    mrb_io_t *fp;
    int fd, fmode, oflags = O_RDONLY;
    convconfig_t convconfig;
    mrb_value opt;
#if defined(HAVE_FCNTL) && defined(F_GETFL)
    int ofmode;
#else
    struct stat st;
#endif

mrb_secure(mrb_state *mrb, 4);

    argc = mrb_scan_args(argc, argv, "11:", &fnum, &vmode, &opt);
mrb_io_extract_modeenc(mrb_state *mrb, &vmode, 0, opt, &oflags, &fmode, &convconfig);

    fd = NUM2INT(fnum);
    if (mrb_reserved_fd_p(fd)) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "The given fd is not accessible because RubyVM reserves it");
    }
#if defined(HAVE_FCNTL) && defined(F_GETFL)
    oflags = fcntl(fd, F_GETFL);
    if (oflags == -1) mrb_sys_fail(0);
#else
    if (fstat(fd, &st) == -1) mrb_sys_fail(0);
#endif
mrb_update_max_fd(mrb_state *mrb, fd);
#if defined(HAVE_FCNTL) && defined(F_GETFL)
    ofmode = mrb_io_oflags_fmode(oflags);
    if (NIL_P(vmode)) {
	fmode = ofmode;
    }
    else if ((~ofmode & fmode) & FMODE_READWRITE) {
	mrb_value error = INT2FIX(EINVAL);
	mrb_exc_raise(mrb_state *mrb, mrb_class_new_instance(1, &error, mrb_eSystemCallError));
    }
#endif
    if (!NIL_P(opt) && mrb_hash_aref(opt, sym_autoclose) == Qfalse) {
	fmode |= FMODE_PREP;
    }
MakeOpenFile(mrb_state *mrb, io, fp);
    fp->fd = fd;
    fp->mode = fmode;
    fp->encs = convconfig;
clear_codeconv(mrb_state *mrb, fp);
io_check_tty(mrb_state *mrb, fp);
    if (fileno(stdin) == fd)
	fp->stdio_file = stdin;
    else if (fileno(stdout) == fd)
	fp->stdio_file = stdout;
    else if (fileno(stderr) == fd)
	fp->stdio_file = stderr;

    if (fmode & FMODE_SETENC_BY_BOM) io_set_encoding_by_bom(io);
    return io;
}

/*
 *  call-seq:
 *     File.new(filename, mode="r" [, opt])            -> file
 *     File.new(filename [, mode [, perm]] [, opt])    -> file
 *
 *  Opens the file named by +filename+ according to +mode+ (default is "r")
 *  and returns a new <code>File</code> object.
 *
 *  === Parameters
 *
 *  See the description of class IO for a description of +mode+.  The file
 *  mode may optionally be specified as a Fixnum by +or+-ing together the
 *  flags (O_RDONLY etc, again described under +IO+).
 *
 *  Optional permission bits may be given in +perm+.  These mode and
 *  permission bits are platform dependent; on Unix systems, see
 *  <code>open(2)</code> for details.
 *
 *  Optional +opt+ parameter is same as in IO.open.
 *
 *  === Examples
 *
 *    f = File.new("testfile", "r")
 *    f = File.new("newfile",  "w+")
 *    f = File.new("newfile", File::CREAT|File::TRUNC|File::RDWR, 0644)
 */

static mrb_value
mrb_file_initialize(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    if (RFILE(io)->fptr) {
	mrb_raise(mrb_state *mrb, mrb_eRuntimeError, "reinitializing File");
    }
    if (0 < argc && argc < 3) {
	mrb_value fd = mrb_check_convert_type(argv[0], T_FIXNUM, "Fixnum", "to_int");

	if (!NIL_P(fd)) {
	    argv[0] = fd;
	    return mrb_io_initialize(argc, argv, io);
	}
    }
mrb_open_file(mrb_state *mrb, argc, argv, io);

    return io;
}

/* :nodoc: */
static mrb_value
mrb_io_s_new(mrb_state *mrb, int argc, mrb_value *argv, mrb_value klass)
{
    if (mrb_block_given_p()) {
	const char *cname = mrb_class2name(klass);

	mrb_warn(mrb_state *mrb, "%s::new() does not take block; use %s::open() instead",
		cname, cname);
    }
    return mrb_class_new_instance(argc, argv, klass);
}


/*
 *  call-seq:
 *     IO.for_fd(fd, mode [, opt])    -> io
 *
 *  Synonym for <code>IO.new</code>.
 *
 */

static mrb_value
mrb_io_s_for_fd(mrb_state *mrb, int argc, mrb_value *argv, mrb_value klass)
{
    mrb_value io = mrb_obj_alloc(klass);
mrb_io_initialize(mrb_state *mrb, argc, argv, io);
    return io;
}

/*
 *  call-seq:
 *     ios.autoclose?   -> true or false
 *
 *  Returns +true+ if the underlying file descriptor of _ios_ will be
 *  closed automatically at its finalization, otherwise +false+.
 */

static mrb_value
mrb_io_autoclose_p(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;
mrb_secure(mrb_state *mrb, 4);
GetOpenFile(mrb_state *mrb, io, fptr);
    return (fptr->mode & FMODE_PREP) ? Qfalse : Qtrue;
}

/*
 *  call-seq:
 *     io.autoclose = bool    -> true or false
 *
 *  Sets auto-close flag.
 *
 *     f = open("/dev/null")
 *     IO.for_fd(f.fileno)
 *     # ...
 *     f.gets # may cause IOError
 *
 *     f = open("/dev/null")
 *     IO.for_fd(f.fileno).autoclose = true
 *     # ...
 *     f.gets # won't cause IOError
 */

static mrb_value
mrb_io_set_autoclose(mrb_state *mrb, mrb_value io, mrb_value autoclose)
{
    mrb_io_t *fptr;
mrb_secure(mrb_state *mrb, 4);
GetOpenFile(mrb_state *mrb, io, fptr);
    if (!RTEST(autoclose))
	fptr->mode |= FMODE_PREP;
    else
	fptr->mode &= ~FMODE_PREP;
    return io;
}

static void
argf_mark(mrb_state *mrb, void *ptr)
{
    struct argf *p = ptr;
mrb_gc_mark(mrb_state *mrb, p->filename);
mrb_gc_mark(mrb_state *mrb, p->current_file);
mrb_gc_mark(mrb_state *mrb, p->argv);
mrb_gc_mark(mrb_state *mrb, p->encs.ecopts);
}

static void
argf_free(mrb_state *mrb, void *ptr)
{
    struct argf *p = ptr;
xfree(mrb_state *mrb, p->inplace);
xfree(mrb_state *mrb, p);
}

static inline void
argf_init(mrb_state *mrb, struct argf *p, mrb_value v)
{
    p->filename = Qnil;
    p->current_file = Qnil;
    p->lineno = 0;
    p->argv = v;
}

static mrb_value
argf_alloc(mrb_state *mrb, mrb_value klass)
{
    struct argf *p;
    mrb_value argf = Data_Make_Struct(klass, struct argf, argf_mark, argf_free, p);

argf_init(mrb_state *mrb, p, Qnil);
    return argf;
}

#undef mrb_argv

/* :nodoc: */
static mrb_value
argf_initialize(mrb_state *mrb, mrb_value argf, mrb_value argv)
{
memset(mrb_state *mrb, &ARGF, 0, sizeof(ARGF));
argf_init(mrb_state *mrb, &ARGF, argv);

    return argf;
}

/* :nodoc: */
static mrb_value
argf_initialize_copy(mrb_state *mrb, mrb_value argf, mrb_value orig)
{
    ARGF = argf_of(orig);
    ARGF.argv = mrb_obj_dup(ARGF.argv);
    if (ARGF.inplace) {
	const char *inplace = ARGF.inplace;
	ARGF.inplace = 0;
	ARGF.inplace = ruby_strdup(inplace);
    }
    return argf;
}

/*
 *  call-seq:
 *     ARGF.lineno = number  -> nil
 *
 *  Sets the line number of +ARGF+ as a whole to the given +Integer+.
 *
 *  +ARGF+ sets the line number automatically as you read data, so normally
 *  you will not need to set it explicitly. To access the current line number
 *  use +ARGF.lineno+.
 *
 *  For example:
 *
 *      ARGF.lineno      #=> 0
 *      ARGF.readline    #=> "This is line 1\n"
 *      ARGF.lineno      #=> 1
 *      ARGF.lineno = 0  #=> nil
 *      ARGF.lineno      #=> 0
 */
static mrb_value
argf_set_lineno(mrb_state *mrb, mrb_value argf, mrb_value val)
{
    ARGF.lineno = NUM2INT(val);
    ARGF.last_lineno = ARGF.lineno;
    return Qnil;
}

/*
 *  call-seq:
 *     ARGF.lineno -> integer
 *
 *  Returns the current line number of ARGF as a whole. This value
 *  can be set manually with +ARGF.lineno=+.
 *
 *  For example:
 *
 *      ARGF.lineno   #=> 0
 *      ARGF.readline #=> "This is line 1\n"
 *      ARGF.lineno   #=> 1
 */
static mrb_value
argf_lineno(mrb_state *mrb, mrb_value argf)
{
    return INT2FIX(ARGF.lineno);
}

static mrb_value
argf_forward(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    return mrb_funcall3(ARGF.current_file, mrb_frame_this_func(), argc, argv);
}

#define next_argv() argf_next_argv(argf)
#define ARGF_GENERIC_INPUT_P() \
(mrb_state *mrb, ARGF.current_file == mrb_stdin && TYPE(ARGF.current_file) != T_FILE)
#define ARGF_FORWARD(argc, argv) do {\
    if (ARGF_GENERIC_INPUT_P())\
	return argf_forward((argc), (argv), argf);\
} while (0)
#define NEXT_ARGF_FORWARD(argc, argv) do {\
    if (!next_argv()) return Qnil;\
ARGF_FORWARD(mrb_state *mrb, (argc), (argv));\
} while (0)

static void
argf_close(mrb_state *mrb, mrb_value file)
{
    if (file == mrb_stdin) return;
    if (RB_TYPE_P(file, T_FILE)) {
	mrb_io_set_write_io(mrb_state *mrb, file, Qnil);
    }
mrb_funcall3(mrb_state *mrb, file, mrb_intern("close"), 0, 0);
}

static int
argf_next_argv(mrb_state *mrb, mrb_value argf)
{
    char *fn;
    mrb_io_t *fptr;
    int stdout_binmode = 0;
    int fmode;

    if (TYPE(mrb_stdout) == T_FILE) {
GetOpenFile(mrb_state *mrb, mrb_stdout, fptr);
        if (fptr->mode & FMODE_BINMODE)
            stdout_binmode = 1;
    }

    if (ARGF.init_p == 0) {
	if (!NIL_P(ARGF.argv) && RARRAY_LEN(ARGF.argv) > 0) {
	    ARGF.next_p = 1;
	}
	else {
	    ARGF.next_p = -1;
	}
	ARGF.init_p = 1;
    }
    else {
	if (NIL_P(ARGF.argv)) {
	    ARGF.next_p = -1;
	}
	else if (ARGF.next_p == -1 && RARRAY_LEN(ARGF.argv) > 0) {
	    ARGF.next_p = 1;
	}
    }

    if (ARGF.next_p == 1) {
      retry:
	if (RARRAY_LEN(ARGF.argv) > 0) {
	    ARGF.filename = mrb_ary_shift(ARGF.argv);
	    fn = StringValueCStr(ARGF.filename);
	    if (strlen(fn) == 1 && fn[0] == '-') {
		ARGF.current_file = mrb_stdin;
		if (ARGF.inplace) {
		    mrb_warn("Can't do inplace edit for stdio; skipping");
		    goto retry;
		}
	    }
	    else {
		mrb_value write_io = Qnil;
		int fr = mrb_sysopen(ARGF.filename, O_RDONLY, 0);

		if (ARGF.inplace) {
		    struct stat st;
#ifndef NO_SAFE_RENAME
		    struct stat st2;
#endif
		    mrb_value str;
		    int fw;

		    if (TYPE(mrb_stdout) == T_FILE && mrb_stdout != orig_stdout) {
			mrb_io_close(mrb_state *mrb, mrb_stdout);
		    }
		    fstat(fr, &st);
		    if (*ARGF.inplace) {
			str = mrb_str_new2(fn);
			mrb_str_cat2(mrb_state *mrb, str, ARGF.inplace);
#ifdef NO_SAFE_RENAME
			(mrb_state *mrb, void)close(fr);
			(mrb_state *mrb, void)unlink(RSTRING_PTR(str));
			if (rename(fn, RSTRING_PTR(str)) < 0) {
			    mrb_warn("Can't rename %s to %s: %s, skipping file",
				    fn, RSTRING_PTR(str), strerror(errno));
			    goto retry;
			}
			fr = mrb_sysopen(str, O_RDONLY, 0);
#else
			if (rename(fn, RSTRING_PTR(str)) < 0) {
			    mrb_warn("Can't rename %s to %s: %s, skipping file",
				    fn, RSTRING_PTR(str), strerror(errno));
			    close(fr);
			    goto retry;
			}
#endif
		    }
		    else {
#ifdef NO_SAFE_RENAME
			mrb_fatal(mrb_state *mrb, "Can't do inplace edit without backup");
#else
			if (unlink(fn) < 0) {
			    mrb_warn("Can't remove %s: %s, skipping file",
				    fn, strerror(errno));
			    close(fr);
			    goto retry;
			}
#endif
		    }
		    fw = mrb_sysopen(ARGF.filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
#ifndef NO_SAFE_RENAME
		    fstat(fw, &st2);
#ifdef HAVE_FCHMOD
		    fchmod(fw, st.st_mode);
#else
		    chmod(fn, st.st_mode);
#endif
		    if (st.st_uid!=st2.st_uid || st.st_gid!=st2.st_gid) {
			int err;
#ifdef HAVE_FCHOWN
			err = fchown(fw, st.st_uid, st.st_gid);
#else
			err = chown(fn, st.st_uid, st.st_gid);
#endif
			if (err && getuid() == 0 && st2.st_uid == 0) {
			    const char *wkfn = RSTRING_PTR(ARGF.filename);
			    mrb_warn("Can't set owner/group of %s to same as %s: %s, skipping file",
				    wkfn, fn, strerror(errno));
			    (void)close(fr);
			    (void)close(fw);
			    (void)unlink(wkfn);
			    goto retry;
			}
		    }
#endif
		    write_io = prep_io(fw, FMODE_WRITABLE, mrb_cFile, fn);
		    mrb_stdout = write_io;
		    if (stdout_binmode) mrb_io_binmode(mrb_stdout);
		}
		fmode = FMODE_READABLE;
		if (!ARGF.binmode) {
		    fmode |= DEFAULT_TEXTMODE;
		}
		ARGF.current_file = prep_io(fr, fmode, mrb_cFile, fn);
		if (!NIL_P(write_io)) {
		    mrb_io_set_write_io(ARGF.current_file, write_io);
		}
	    }
	    if (ARGF.binmode) mrb_io_ascii8bit_binmode(ARGF.current_file);
	    GetOpenFile(ARGF.current_file, fptr);
	    if (ARGF.encs.enc) {
		fptr->encs = ARGF.encs;
clear_codeconv(mrb_state *mrb, fptr);
	    }
	    else {
		fptr->encs.ecflags &= ~ECONV_NEWLINE_DECORATOR_MASK;
		if (!ARGF.binmode) {
		    fptr->encs.ecflags |= ECONV_DEFAULT_NEWLINE_DECORATOR;
#ifdef TEXTMODE_NEWLINE_DECORATOR_ON_WRITE
		    fptr->encs.ecflags |= TEXTMODE_NEWLINE_DECORATOR_ON_WRITE;
#endif
		}
	    }
	    ARGF.next_p = 0;
	}
	else {
	    ARGF.next_p = 1;
	    return FALSE;
	}
    }
    else if (ARGF.next_p == -1) {
	ARGF.current_file = mrb_stdin;
	ARGF.filename = mrb_str_new2("-");
	if (ARGF.inplace) {
	    mrb_warn("Can't do inplace edit for stdio");
	    mrb_stdout = orig_stdout;
	}
    }
    return TRUE;
}

static mrb_value
argf_getline(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    mrb_value line;
    long lineno = ARGF.lineno;

  retry:
    if (!next_argv()) return Qnil;
    if (ARGF_GENERIC_INPUT_P()) {
	line = mrb_funcall3(ARGF.current_file, mrb_intern("gets"), argc, argv);
    }
    else {
	if (argc == 0 && mrb_rs == mrb_default_rs) {
	    line = mrb_io_gets(ARGF.current_file);
	}
	else {
	    line = mrb_io_getline(argc, argv, ARGF.current_file);
	}
	if (NIL_P(line) && ARGF.next_p != -1) {
	    argf_close(ARGF.current_file);
	    ARGF.next_p = 1;
	    goto retry;
	}
    }
    if (!NIL_P(line)) {
	ARGF.lineno = ++lineno;
	ARGF.last_lineno = ARGF.lineno;
    }
    return line;
}

static mrb_value
argf_lineno_getter(mrb_state *mrb, ID id, mrb_value *var)
{
    mrb_value argf = *var;
    return INT2FIX(ARGF.last_lineno);
}

static void
argf_lineno_setter(mrb_state *mrb, mrb_value val, ID id, mrb_value *var)
{
    mrb_value argf = *var;
    int n = NUM2INT(val);
    ARGF.last_lineno = ARGF.lineno = n;
}

static mrb_value argf_gets(int, mrb_value *, mrb_value);

/*
 *  call-seq:
 *     gets(sep=$/)    -> string or nil
 *     gets(limit)     -> string or nil
 *     gets(sep,limit) -> string or nil
 *
 *  Returns (and assigns to <code>$_</code>) the next line from the list
 *  of files in +ARGV+ (or <code>$*</code>), or from standard input if
 *  no files are present on the command line. Returns +nil+ at end of
 *  file. The optional argument specifies the record separator. The
 *  separator is included with the contents of each record. A separator
 *  of +nil+ reads the entire contents, and a zero-length separator
 *  reads the input one paragraph at a time, where paragraphs are
 *  divided by two consecutive newlines.  If the first argument is an
 *  integer, or optional second argument is given, the returning string
 *  would not be longer than the given value in bytes.  If multiple
 *  filenames are present in +ARGV+, +gets(nil)+ will read the contents
 *  one file at a time.
 *
 *     ARGV << "testfile"
 *     print while gets
 *
 *  <em>produces:</em>
 *
 *     This is line one
 *     This is line two
 *     This is line three
 *     And so on...
 *
 *  The style of programming using <code>$_</code> as an implicit
 *  parameter is gradually losing favor in the Ruby community.
 */

static mrb_value
mrb_f_gets(mrb_state *mrb, int argc, mrb_value *argv, mrb_value recv)
{
    if (recv == argf) {
	return argf_gets(argc, argv, argf);
    }
    return mrb_funcall2(argf, mrb_intern("gets"), argc, argv);
}

/*
 *  call-seq:
 *     ARGF.gets(sep=$/)     -> string
 *     ARGF.gets(limit)      -> string
 *     ARGF.gets(sep, limit) -> string
 *
 *  Returns the next line from the current file in +ARGF+.
 *
 *  By default lines are assumed to be separated by +$/+; to use a different
 *  character as a separator, supply it as a +String+ for the _sep_ argument.
 *
 *  The optional  _limit_ argument specifies how many characters of each line
 *  to return. By default all characters are returned.
 *
 */
static mrb_value
argf_gets(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    mrb_value line;

    line = argf_getline(argc, argv, argf);
mrb_lastline_set(mrb_state *mrb, line);

    return line;
}

mrb_value
mrb_gets(mrb_state *mrb, void)
{
    mrb_value line;

    if (mrb_rs != mrb_default_rs) {
	return mrb_f_gets(0, 0, argf);
    }

  retry:
    if (!next_argv()) return Qnil;
    line = mrb_io_gets(ARGF.current_file);
    if (NIL_P(line) && ARGF.next_p != -1) {
	mrb_io_close(mrb_state *mrb, ARGF.current_file);
	ARGF.next_p = 1;
	goto retry;
    }
mrb_lastline_set(mrb_state *mrb, line);
    if (!NIL_P(line)) {
	ARGF.lineno++;
	ARGF.last_lineno = ARGF.lineno;
    }

    return line;
}

static mrb_value argf_readline(int, mrb_value *, mrb_value);

/*
 *  call-seq:
 *     readline(sep=$/)     -> string
 *     readline(limit)      -> string
 *     readline(sep, limit) -> string
 *
 *  Equivalent to <code>Kernel::gets</code>, except
 *  +readline+ raises +EOFError+ at end of file.
 */

static mrb_value
mrb_f_readline(mrb_state *mrb, int argc, mrb_value *argv, mrb_value recv)
{
    if (recv == argf) {
	return argf_readline(argc, argv, argf);
    }
    return mrb_funcall2(argf, mrb_intern("readline"), argc, argv);
}


/*
 *  call-seq:
 *     ARGF.readline(sep=$/)     -> string
 *     ARGF.readline(limit)      -> string
 *     ARGF.readline(sep, limit) -> string
 *
 *  Returns the next line from the current file in +ARGF+.
 *
 *  By default lines are assumed to be separated by +$/+; to use a different
 *  character as a separator, supply it as a +String+ for the _sep_ argument.
 *
 *  The optional  _limit_ argument specifies how many characters of each line
 *  to return. By default all characters are returned.
 *
 *  An +EOFError+ is raised at the end of the file.
 */
static mrb_value
argf_readline(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    mrb_value line;

    if (!next_argv()) mrb_eof_error();
ARGF_FORWARD(mrb_state *mrb, argc, argv);
    line = argf_gets(argc, argv, argf);
    if (NIL_P(line)) {
	mrb_eof_error(mrb_state *mrb, );
    }

    return line;
}

static mrb_value argf_readlines(int, mrb_value *, mrb_value);

/*
 *  call-seq:
 *     readlines(sep=$/)    -> array
 *     readlines(limit)     -> array
 *     readlines(sep,limit) -> array
 *
 *  Returns an array containing the lines returned by calling
 *  <code>Kernel.gets(<i>sep</i>)</code> until the end of file.
 */

static mrb_value
mrb_f_readlines(mrb_state *mrb, int argc, mrb_value *argv, mrb_value recv)
{
    if (recv == argf) {
	return argf_readlines(argc, argv, argf);
    }
    return mrb_funcall2(argf, mrb_intern("readlines"), argc, argv);
}

/*
 *  call-seq:
 *     ARGF.readlines(sep=$/)     -> array
 *     ARGF.readlines(limit)      -> array
 *     ARGF.readlines(sep, limit) -> array
 *
 *     ARGF.to_a(sep=$/)     -> array
 *     ARGF.to_a(limit)      -> array
 *     ARGF.to_a(sep, limit) -> array
 *
 *  Reads +ARGF+'s current file in its entirety, returning an +Array+ of its
 *  lines, one line per element. Lines are assumed to be separated by _sep_.
 *
 *     lines = ARGF.readlines
 *     lines[0]                #=> "This is line one\n"
 */
static mrb_value
argf_readlines(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    long lineno = ARGF.lineno;
    mrb_value lines, ary;

    ary = mrb_ary_new();
    while (next_argv()) {
	if (ARGF_GENERIC_INPUT_P()) {
	    lines = mrb_funcall3(ARGF.current_file, mrb_intern("readlines"), argc, argv);
	}
	else {
	    lines = mrb_io_readlines(argc, argv, ARGF.current_file);
	    argf_close(ARGF.current_file);
	}
	ARGF.next_p = 1;
	mrb_ary_concat(mrb_state *mrb, ary, lines);
	ARGF.lineno = lineno + RARRAY_LEN(ary);
	ARGF.last_lineno = ARGF.lineno;
    }
    ARGF.init_p = 0;
    return ary;
}

/*
 *  call-seq:
 *     `cmd`    -> string
 *
 *  Returns the standard output of running _cmd_ in a subshell.
 *  The built-in syntax <code>%x{...}</code> uses
 *  this method. Sets <code>$?</code> to the process status.
 *
 *     `date`                   #=> "Wed Apr  9 08:56:30 CDT 2003\n"
 *     `ls testdir`.split[1]    #=> "main.rb"
 *     `echo oops && exit 99`   #=> "oops\n"
 *     $?.exitstatus            #=> 99
 */

static mrb_value
mrb_f_backquote(mrb_state *mrb, mrb_value obj, mrb_value str)
{
    volatile mrb_value port;
    mrb_value result;
    mrb_io_t *fptr;

SafeStringValue(mrb_state *mrb, str);
    port = pipe_open_s(str, "r", FMODE_READABLE|DEFAULT_TEXTMODE, NULL);
    if (NIL_P(port)) return mrb_str_new(0,0);

GetOpenFile(mrb_state *mrb, port, fptr);
    result = read_all(fptr, remain_size(fptr), Qnil);
mrb_io_close(mrb_state *mrb, port);

    return result;
}

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

static mrb_value
select_internal(mrb_state *mrb, mrb_value read, mrb_value write, mrb_value except, struct timeval *tp, mrb_fdset_t *fds)
{
    mrb_value res, list;
    mrb_fdset_t *rp, *wp, *ep;
    mrb_io_t *fptr;
    long i;
    int max = 0, n;
    int pending = 0;
    struct timeval timerec;

    if (!NIL_P(read)) {
	Check_Type(mrb_state *mrb, read, T_ARRAY);
	for (i=0; i<RARRAY_LEN(read); i++) {
	    GetOpenFile(mrb_io_get_io(RARRAY_PTR(read)[i]), fptr);
	    mrb_fd_set(fptr->fd, &fds[0]);
	    if (READ_DATA_PENDING(fptr) || READ_CHAR_PENDING(fptr)) { /* check for buffered data */
		pending++;
		mrb_fd_set(mrb_state *mrb, fptr->fd, &fds[3]);
	    }
	    if (max < fptr->fd) max = fptr->fd;
	}
	if (pending) {		/* no blocking if there's buffered data */
	    timerec.tv_sec = timerec.tv_usec = 0;
	    tp = &timerec;
	}
	rp = &fds[0];
    }
    else
	rp = 0;

    if (!NIL_P(write)) {
	Check_Type(mrb_state *mrb, write, T_ARRAY);
	for (i=0; i<RARRAY_LEN(write); i++) {
            mrb_value write_io = GetWriteIO(mrb_io_get_io(RARRAY_PTR(write)[i]));
	    GetOpenFile(write_io, fptr);
	    mrb_fd_set(fptr->fd, &fds[1]);
	    if (max < fptr->fd) max = fptr->fd;
	}
	wp = &fds[1];
    }
    else
	wp = 0;

    if (!NIL_P(except)) {
	Check_Type(mrb_state *mrb, except, T_ARRAY);
	for (i=0; i<RARRAY_LEN(except); i++) {
            mrb_value io = mrb_io_get_io(RARRAY_PTR(except)[i]);
            mrb_value write_io = GetWriteIO(io);
	    GetOpenFile(io, fptr);
	    mrb_fd_set(fptr->fd, &fds[2]);
	    if (max < fptr->fd) max = fptr->fd;
            if (io != write_io) {
GetOpenFile(mrb_state *mrb, write_io, fptr);
mrb_fd_set(mrb_state *mrb, fptr->fd, &fds[2]);
                if (max < fptr->fd) max = fptr->fd;
            }
	}
	ep = &fds[2];
    }
    else {
	ep = 0;
    }

    max++;

    n = mrb_thread_fd_select(max, rp, wp, ep, tp);
    if (n < 0) {
	mrb_sys_fail(mrb_state *mrb, 0);
    }
    if (!pending && n == 0) return Qnil; /* returns nil on timeout */

    res = mrb_ary_new2(3);
mrb_ary_push(mrb_state *mrb, res, rp?mrb_ary_new():mrb_ary_new2(0));
mrb_ary_push(mrb_state *mrb, res, wp?mrb_ary_new():mrb_ary_new2(0));
mrb_ary_push(mrb_state *mrb, res, ep?mrb_ary_new():mrb_ary_new2(0));

    if (rp) {
	list = RARRAY_PTR(res)[0];
	for (i=0; i< RARRAY_LEN(read); i++) {
	    mrb_value obj = mrb_ary_entry(read, i);
	    mrb_value io = mrb_io_get_io(obj);
	    GetOpenFile(io, fptr);
	    if (mrb_fd_isset(fptr->fd, &fds[0]) ||
		mrb_fd_isset(mrb_state *mrb, fptr->fd, &fds[3])) {
		mrb_ary_push(mrb_state *mrb, list, obj);
	    }
	}
    }

    if (wp) {
	list = RARRAY_PTR(res)[1];
	for (i=0; i< RARRAY_LEN(write); i++) {
	    mrb_value obj = mrb_ary_entry(write, i);
	    mrb_value io = mrb_io_get_io(obj);
	    mrb_value write_io = GetWriteIO(io);
	    GetOpenFile(write_io, fptr);
	    if (mrb_fd_isset(fptr->fd, &fds[1])) {
		mrb_ary_push(mrb_state *mrb, list, obj);
	    }
	}
    }

    if (ep) {
	list = RARRAY_PTR(res)[2];
	for (i=0; i< RARRAY_LEN(except); i++) {
	    mrb_value obj = mrb_ary_entry(except, i);
	    mrb_value io = mrb_io_get_io(obj);
	    mrb_value write_io = GetWriteIO(io);
	    GetOpenFile(io, fptr);
	    if (mrb_fd_isset(fptr->fd, &fds[2])) {
		mrb_ary_push(mrb_state *mrb, list, obj);
	    }
	    else if (io != write_io) {
		GetOpenFile(mrb_state *mrb, write_io, fptr);
		if (mrb_fd_isset(fptr->fd, &fds[2])) {
		    mrb_ary_push(list, obj);
		}
	    }
	}
    }

    return res;			/* returns an empty array on interrupt */
}

struct select_args {
    mrb_value read, write, except;
    struct timeval *timeout;
    mrb_fdset_t fdsets[4];
};

static mrb_value
select_call(mrb_state *mrb, mrb_value arg)
{
    struct select_args *p = (struct select_args *)arg;

    return select_internal(p->read, p->write, p->except, p->timeout, p->fdsets);
}

static mrb_value
select_end(mrb_state *mrb, mrb_value arg)
{
    struct select_args *p = (struct select_args *)arg;
    int i;

    for (i = 0; i < numberof(p->fdsets); ++i)
	mrb_fd_term(mrb_state *mrb, &p->fdsets[i]);
    return Qnil;
}

static mrb_value sym_normal,   sym_sequential, sym_random,
             sym_willneed, sym_dontneed, sym_noreuse;

#ifdef HAVE_POSIX_FADVISE
struct io_advise_struct {
    int fd;
    off_t offset;
    off_t len;
    int advice;
};

static mrb_value
io_advise_internal(mrb_state *mrb, void *arg)
{
    struct io_advise_struct *ptr = arg;
    return posix_fadvise(ptr->fd, ptr->offset, ptr->len, ptr->advice);
}

static mrb_value
io_advise_sym_to_const(mrb_state *mrb, mrb_value sym)
{
#ifdef POSIX_FADV_NORMAL
    if (sym == sym_normal)
	return INT2NUM(POSIX_FADV_NORMAL);
#endif

#ifdef POSIX_FADV_RANDOM
    if (sym == sym_random)
	return INT2NUM(POSIX_FADV_RANDOM);
#endif

#ifdef POSIX_FADV_SEQUENTIAL
    if (sym == sym_sequential)
	return INT2NUM(POSIX_FADV_SEQUENTIAL);
#endif

#ifdef POSIX_FADV_WILLNEED
    if (sym == sym_willneed)
	return INT2NUM(POSIX_FADV_WILLNEED);
#endif

#ifdef POSIX_FADV_DONTNEED
    if (sym == sym_dontneed)
	return INT2NUM(POSIX_FADV_DONTNEED);
#endif

#ifdef POSIX_FADV_NOREUSE
    if (sym == sym_noreuse)
	return INT2NUM(POSIX_FADV_NOREUSE);
#endif

    return Qnil;
}

static mrb_value
do_io_advise(mrb_state *mrb, mrb_io_t *fptr, mrb_value advice, off_t offset, off_t len)
{
    int rv;
    struct io_advise_struct ias;
    mrb_value num_adv;

    num_adv = io_advise_sym_to_const(advice);

    /*
     * The platform doesn't support this hint. We don't raise exception, instead
     * silently ignore it. Because IO::advise is only hint.
     */
    if (num_adv == Qnil)
	return Qnil;

    ias.fd     = fptr->fd;
    ias.advice = NUM2INT(num_adv);
    ias.offset = offset;
    ias.len    = len;

    rv = (int)mrb_thread_io_blocking_region(io_advise_internal, &ias, fptr->fd);
    if (rv)
	/* posix_fadvise(2) doesn't set errno. On success it returns 0; otherwise
	   it returns the error code. */
	mrb_syserr_fail(mrb_state *mrb, rv, RSTRING_PTR(fptr->pathv));

    return Qnil;
}

#endif /* HAVE_POSIX_FADVISE */

static void
advice_arg_check(mrb_state *mrb, mrb_value advice)
{
    if (!SYMBOL_P(advice))
	mrb_raise(mrb_state *mrb, mrb_eTypeError, "advice must be a Symbol");

    if (advice != sym_normal &&
	advice != sym_sequential &&
	advice != sym_random &&
	advice != sym_willneed &&
	advice != sym_dontneed &&
	advice != sym_noreuse) {
	mrb_value symname = mrb_inspect(advice);
	mrb_raise(mrb_state *mrb, mrb_eNotImpError, "Unsupported advice: %s",
		 StringValuePtr(symname));
    }
}

/*
 *  call-seq:
 *     ios.advise(advice, offset=0, len=0) -> nil
 *
 *  Announce an intention to access data from the current file in a
 *  specific pattern. On platforms that do not support the
 *  <em>posix_fadvise(2)</em> system call, this method is a no-op.
 *
 * _advice_ is one of the following symbols:
 *
 *  * :normal - No advice to give; the default assumption for an open file.
 *  * :sequential - The data will be accessed sequentially:
 *     with lower offsets read before higher ones.
 *  * :random - The data will be accessed in random order.
 *  * :willneed - The data will be accessed in the near future.
 *  * :dontneed - The data will not be accessed in the near future.
 *  * :noreuse - The data will only be accessed once.
 *
 * The semantics of a piece of advice are platform-dependent. See
 * <em>man 2 posix_fadvise</em> for details.
 *
 *  "data" means the region of the current file that begins at
 *  _offset_ and extends for _len_ bytes. If _len_ is 0, the region
 *  ends at the last byte of the file. By default, both _offset_ and
 *  _len_ are 0, meaning that the advice applies to the entire file.
 *
 *  If an error occurs, one of the following exceptions will be raised:
 *
 *  * <code>IOError</code> - The <code>IO</code> stream is closed.
 *  * <code>Errno::EBADF</code> - The file descriptor of the current file is
      invalid.
 *  * <code>Errno::EINVAL</code> - An invalid value for _advice_ was given.
 *  * <code>Errno::ESPIPE</code> - The file descriptor of the current
 *  * file refers to a FIFO or pipe. (Linux raises <code>Errno::EINVAL</code>
 *  * in this case).
 *  * <code>TypeError</code> - Either _advice_ was not a Symbol, or one of the
      other arguments was not an <code>Integer</code>.
 *  * <code>RangeError</code> - One of the arguments given was too big/small.
 *
 * This list is not exhaustive; other Errno:: exceptions are also possible.
 */
static mrb_value
mrb_io_advise(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value advice, offset, len;
    off_t off, l;
    mrb_io_t *fptr;

mrb_scan_args(mrb_state *mrb, argc, argv, "12", &advice, &offset, &len);
advice_arg_check(mrb_state *mrb, advice);

    io = GetWriteIO(io);
GetOpenFile(mrb_state *mrb, io, fptr);

    off = NIL_P(offset) ? 0 : NUM2OFFT(offset);
    l   = NIL_P(len)    ? 0 : NUM2OFFT(len);

#ifdef HAVE_POSIX_FADVISE
    return do_io_advise(fptr, advice, off, l);
#else
    /* Ignore all hint */
    return Qnil;
#endif
}

/*
 *  call-seq:
 *     IO.select(read_array
 *               [, write_array
 *               [, error_array
 *               [, timeout]]]) -> array  or  nil
 *
 *  Calls select(2) system call.
 *  It monitors given arrays of <code>IO</code> objects, waits one or more
 *  of <code>IO</code> objects ready for reading, are ready for writing,
 *  and have pending exceptions respectably, and returns an array that
 *  contains arrays of those IO objects.  It will return <code>nil</code>
 *  if optional <i>timeout</i> value is given and no <code>IO</code> object
 *  is ready in <i>timeout</i> seconds.
 *
 *  === Parameters
 *  read_array:: an array of <code>IO</code> objects that wait until ready for read
 *  write_array:: an array of <code>IO</code> objects that wait until ready for write
 *  error_array:: an array of <code>IO</code> objects that wait for exceptions
 *  timeout:: a numeric value in second
 *
 *  === Example
 *
 *      rp, wp = IO.pipe
 *      mesg = "ping "
 *      100.times {
 *        rs, ws, = IO.select([rp], [wp])
 *        if r = rs[0]
 *          ret = r.read(5)
 *          print ret
 *          case ret
 *          when /ping/
 *            mesg = "pong\n"
 *          when /pong/
 *            mesg = "ping "
 *          end
 *        end
 *        if w = ws[0]
 *          w.write(mesg)
 *        end
 *      }
 *
 *  <em>produces:</em>
 *
 *      ping pong
 *      ping pong
 *      ping pong
 *      (snipped)
 *      ping
 */

static mrb_value
mrb_f_select(mrb_state *mrb, int argc, mrb_value *argv, mrb_value obj)
{
    mrb_value timeout;
    struct select_args args;
    struct timeval timerec;
    int i;

mrb_scan_args(mrb_state *mrb, argc, argv, "13", &args.read, &args.write, &args.except, &timeout);
    if (NIL_P(timeout)) {
	args.timeout = 0;
    }
    else {
	timerec = mrb_time_interval(timeout);
	args.timeout = &timerec;
    }

    for (i = 0; i < numberof(args.fdsets); ++i)
	mrb_fd_init(mrb_state *mrb, &args.fdsets[i]);

    return mrb_ensure(select_call, (mrb_value)&args, select_end, (mrb_value)&args);
}

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
 typedef unsigned long ioctl_req_t;
 #define NUM2IOCTLREQ(num) NUM2ULONG(num)
#else
 typedef int ioctl_req_t;
 #define NUM2IOCTLREQ(num) NUM2INT(num)
#endif

struct ioctl_arg {
    int		fd;
    ioctl_req_t	cmd;
    long	narg;
};

static mrb_value nogvl_ioctl(void *ptr)
{
    struct ioctl_arg *arg = ptr;

    return (mrb_value)ioctl(arg->fd, arg->cmd, arg->narg);
}

static int
do_ioctl(mrb_state *mrb, int fd, ioctl_req_t cmd, long narg)
{
    int retval;
    struct ioctl_arg arg;

    arg.fd = fd;
    arg.cmd = cmd;
    arg.narg = narg;

    retval = (int)mrb_thread_io_blocking_region(nogvl_ioctl, &arg, fd);

    return retval;
}

#define DEFULT_IOCTL_NARG_LEN (256)

#ifdef __linux__
static long
linux_iocparm_len(mrb_state *mrb, ioctl_req_t cmd)
{
    long len;

    if ((cmd & 0xFFFF0000) == 0) {
	/* legacy and unstructured ioctl number. */
	return DEFULT_IOCTL_NARG_LEN;
    }

    len = _IOC_SIZE(cmd);

    /* paranoia check for silly drivers which don't keep ioctl convention */
    if (len < DEFULT_IOCTL_NARG_LEN)
	len = DEFULT_IOCTL_NARG_LEN;

    return len;
}
#endif

static long
ioctl_narg_len(mrb_state *mrb, ioctl_req_t cmd)
{
    long len;

#ifdef IOCPARM_MASK
#ifndef IOCPARM_LEN
#define IOCPARM_LEN(x)  (((x) >> 16) & IOCPARM_MASK)
#endif
#endif
#ifdef IOCPARM_LEN
    len = IOCPARM_LEN(cmd);	/* on BSDish systems we're safe */
#elif defined(__linux__)
    len = linux_iocparm_len(cmd);
#else
    /* otherwise guess at what's safe */
    len = DEFULT_IOCTL_NARG_LEN;
#endif

    return len;
}

#ifdef HAVE_FCNTL
#ifdef __linux__
typedef long fcntl_arg_t;
#else
/* posix */
typedef int fcntl_arg_t;
#endif

static long
fcntl_narg_len(mrb_state *mrb, int cmd)
{
    long len;

    switch (cmd) {
#ifdef F_DUPFD
      case F_DUPFD:
	len = sizeof(fcntl_arg_t);
	break;
#endif
#ifdef F_DUP2FD /* bsd specific */
      case F_DUP2FD:
	len = sizeof(int);
	break;
#endif
#ifdef F_DUPFD_CLOEXEC /* linux specific */
      case F_DUPFD_CLOEXEC:
	len = sizeof(fcntl_arg_t);
	break;
#endif
#ifdef F_GETFD
      case F_GETFD:
	len = 1;
	break;
#endif
#ifdef F_SETFD
      case F_SETFD:
	len = sizeof(fcntl_arg_t);
	break;
#endif
#ifdef F_GETFL
      case F_GETFL:
	len = 1;
	break;
#endif
#ifdef F_SETFL
      case F_SETFL:
	len = sizeof(fcntl_arg_t);
	break;
#endif
#ifdef F_GETOWN
      case F_GETOWN:
	len = 1;
	break;
#endif
#ifdef F_SETOWN
      case F_SETOWN:
	len = sizeof(fcntl_arg_t);
	break;
#endif
#ifdef F_GETOWN_EX /* linux specific */
      case F_GETOWN_EX:
	len = sizeof(struct f_owner_ex);
	break;
#endif
#ifdef F_SETOWN_EX /* linux specific */
      case F_SETOWN_EX:
	len = sizeof(struct f_owner_ex);
	break;
#endif
#ifdef F_GETLK
      case F_GETLK:
	len = sizeof(struct flock);
	break;
#endif
#ifdef F_SETLK
      case F_SETLK:
	len = sizeof(struct flock);
	break;
#endif
#ifdef F_SETLKW
      case F_SETLKW:
	len = sizeof(struct flock);
	break;
#endif
#ifdef F_READAHEAD /* bsd specific */
      case F_READAHEAD:
	len = sizeof(int);
	break;
#endif
#ifdef F_RDAHEAD /* Darwin specific */
      case F_RDAHEAD:
	len = sizeof(int);
	break;
#endif
#ifdef F_GETSIG /* linux specific */
      case F_GETSIG:
	len = 1;
	break;
#endif
#ifdef F_SETSIG /* linux specific */
      case F_SETSIG:
	len = sizeof(fcntl_arg_t);
	break;
#endif
#ifdef F_GETLEASE /* linux specific */
      case F_GETLEASE:
	len = 1;
	break;
#endif
#ifdef F_SETLEASE /* linux specific */
      case F_SETLEASE:
	len = sizeof(fcntl_arg_t);
	break;
#endif
#ifdef F_NOTIFY /* linux specific */
      case F_NOTIFY:
	len = sizeof(fcntl_arg_t);
	break;
#endif

      default:
	len = 256;
	break;
    }

    return len;
}
#else /* HAVE_FCNTL */
static long
fcntl_narg_len(mrb_state *mrb, int cmd)
{
    return 0;
}
#endif /* HAVE_FCNTL */

static long
setup_narg(mrb_state *mrb, ioctl_req_t cmd, mrb_value *argp, int io_p)
{
    long narg = 0;
    mrb_value arg = *argp;

    if (NIL_P(arg) || arg == Qfalse) {
	narg = 0;
    }
    else if (FIXNUM_P(arg)) {
	narg = FIX2LONG(arg);
    }
    else if (arg == Qtrue) {
	narg = 1;
    }
    else {
	mrb_value tmp = mrb_check_string_type(arg);

	if (NIL_P(tmp)) {
	    narg = NUM2LONG(arg);
	}
	else {
	    long len;

	    *argp = arg = tmp;
	    if (io_p)
		len = ioctl_narg_len(cmd);
	    else
		len = fcntl_narg_len((int)cmd);
	    mrb_str_modify(arg);

	    /* expand for data + sentinel. */
	    if (RSTRING_LEN(arg) < len+1) {
		mrb_str_resize(mrb_state *mrb, arg, len+1);
	    }
	    /* a little sanity check here */
	    RSTRING_PTR(arg)[RSTRING_LEN(arg) - 1] = 17;
	    narg = (long)(SIGNED_mrb_value)RSTRING_PTR(arg);
	}
    }
        return narg;
    }

static mrb_value
mrb_ioctl(mrb_state *mrb, mrb_value io, mrb_value req, mrb_value arg)
{
    ioctl_req_t cmd = NUM2IOCTLREQ(req);
    mrb_io_t *fptr;
    long narg;
    int retval;
  
mrb_secure(mrb_state *mrb, 2);

    narg = setup_narg(cmd, &arg, 1);
GetOpenFile(mrb_state *mrb, io, fptr);
    retval = do_ioctl(fptr->fd, cmd, narg);
    if (retval < 0) mrb_sys_fail_path(fptr->pathv);
    if (RB_TYPE_P(arg, T_STRING)) {
	if (RSTRING_PTR(arg)[RSTRING_LEN(arg)-1] != 17)
	    mrb_raise(mrb_eArgError, "return value overflowed string");
	RSTRING_PTR(mrb_state *mrb, arg)[RSTRING_LEN(arg)-1] = '\0';
    }

    return INT2NUM(retval);
}

/*
 *  call-seq:
 *     ios.ioctl(integer_cmd, arg)    -> integer
 *
 *  Provides a mechanism for issuing low-level commands to control or
 *  query I/O devices. Arguments and results are platform dependent. If
 *  <i>arg</i> is a number, its value is passed directly. If it is a
 *  string, it is interpreted as a binary sequence of bytes. On Unix
 *  platforms, see <code>ioctl(2)</code> for details. Not implemented on
 *  all platforms.
 */

static mrb_value
mrb_io_ioctl(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value req, arg;

mrb_scan_args(mrb_state *mrb, argc, argv, "11", &req, &arg);
    return mrb_ioctl(io, req, arg);
}

#ifdef HAVE_FCNTL
struct fcntl_arg {
    int		fd;
    int 	cmd;
    long	narg;
};

static mrb_value nogvl_fcntl(void *ptr)
{
    struct fcntl_arg *arg = ptr;

#if defined(F_DUPFD)
    if (arg->cmd == F_DUPFD)
	return (mrb_value)mrb_cloexec_fcntl_dupfd(arg->fd, (int)arg->narg);
#endif
    return (mrb_value)fcntl(arg->fd, arg->cmd, arg->narg);
}

static int
do_fcntl(mrb_state *mrb, int fd, int cmd, long narg)
{
    int retval;
    struct fcntl_arg arg;

    arg.fd = fd;
    arg.cmd = cmd;
    arg.narg = narg;

    retval = (int)mrb_thread_io_blocking_region(nogvl_fcntl, &arg, fd);
#if defined(F_DUPFD)
    if (retval != -1 && cmd == F_DUPFD) {
	mrb_update_max_fd(mrb_state *mrb, retval);
    }
#endif

    return retval;
}

static mrb_value
mrb_fcntl(mrb_state *mrb, mrb_value io, mrb_value req, mrb_value arg)
{
    int cmd = NUM2INT(req);
    mrb_io_t *fptr;
    long narg;
    int retval;

mrb_secure(mrb_state *mrb, 2);

    narg = setup_narg(cmd, &arg, 0);
GetOpenFile(mrb_state *mrb, io, fptr);
    retval = do_fcntl(fptr->fd, cmd, narg);
    if (retval < 0) mrb_sys_fail_path(fptr->pathv);
    if (RB_TYPE_P(arg, T_STRING)) {
	if (RSTRING_PTR(arg)[RSTRING_LEN(arg)-1] != 17)
	    mrb_raise(mrb_eArgError, "return value overflowed string");
	RSTRING_PTR(mrb_state *mrb, arg)[RSTRING_LEN(arg)-1] = '\0';
    }

    if (cmd == F_SETFL) {
	if (narg & O_NONBLOCK) {
	    fptr->mode |= FMODE_WSPLIT_INITIALIZED;
	    fptr->mode &= ~FMODE_WSPLIT;
	}
	else {
	    fptr->mode &= ~(FMODE_WSPLIT_INITIALIZED|FMODE_WSPLIT);
	}
    }

    return INT2NUM(retval);
}

/*
 *  call-seq:
 *     ios.fcntl(integer_cmd, arg)    -> integer
 *
 *  Provides a mechanism for issuing low-level commands to control or
 *  query file-oriented I/O streams. Arguments and results are platform
 *  dependent. If <i>arg</i> is a number, its value is passed
 *  directly. If it is a string, it is interpreted as a binary sequence
 *  of bytes (<code>Array#pack</code> might be a useful way to build this
 *  string). On Unix platforms, see <code>fcntl(2)</code> for details.
 *  Not implemented on all platforms.
 */

static mrb_value
mrb_io_fcntl(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value req, arg;

mrb_scan_args(mrb_state *mrb, argc, argv, "11", &req, &arg);
    return mrb_fcntl(io, req, arg);
}
#else
#define mrb_io_fcntl mrb_f_notimplement
#endif

#if defined(HAVE_SYSCALL) || defined(HAVE___SYSCALL)
/*
 *  call-seq:
 *     syscall(num [, args...])   -> integer
 *
 *  Calls the operating system function identified by _num_ and
 *  returns the result of the function or raises SystemCallError if
 *  it failed.
 *
 *  Arguments for the function can follow _num_. They must be either
 *  +String+ objects or +Integer+ objects. A +String+ object is passed
 *  as a pointer to the byte sequence. An +Integer+ object is passed
 *  as an integer whose bit size is same as a pointer.
 *  Up to nine parameters may be passed (14 on the Atari-ST).
 *
 *  The function identified by _num_ is system
 *  dependent. On some Unix systems, the numbers may be obtained from a
 *  header file called <code>syscall.h</code>.
 *
 *     syscall 4, 1, "hello\n", 6   # '4' is write(2) on our box
 *
 *  <em>produces:</em>
 *
 *     hello
 *
 *
 *  Calling +syscall+ on a platform which does not have any way to
 *  an arbitrary system function just fails with NotImplementedError.
 *
 * Note::
 *   +syscall+ is essentially unsafe and unportable. Feel free to shoot your foot.
 *   DL (Fiddle) library is preferred for safer and a bit more portable programming.
 */

static mrb_value
mrb_f_syscall(mrb_state *mrb, int argc, mrb_value *argv)
{
#ifdef atarist
    mrb_value arg[13]; /* yes, we really need that many ! */
#else
    mrb_value arg[8];
#endif
#if SIZEOF_VOIDP == 8 && defined(HAVE___SYSCALL) && SIZEOF_INT != 8 /* mainly *BSD */
# define SYSCALL __syscall
# define NUM2SYSCALLID(x) NUM2LONG(x)
# define RETVAL2NUM(x) LONG2NUM(x)
# if SIZEOF_LONG == 8
    long num, retval = -1;
# elif SIZEOF_LONG_LONG == 8
    long long num, retval = -1;
# else
#  error ---->> it is asserted that __syscall takes the first argument and returns retval in 64bit signed integer. <<----
# endif
#elif defined linux
# define SYSCALL syscall
# define NUM2SYSCALLID(x) NUM2LONG(x)
# define RETVAL2NUM(x) LONG2NUM(x)
    /*
     * Linux man page says, syscall(2) function prototype is below.
     *
     *     int syscall(int number, ...);
     *
     * But, it's incorrect. Actual one takes and returned long. (see unistd.h)
     */
    long num, retval = -1;
#else
# define SYSCALL syscall
# define NUM2SYSCALLID(x) NUM2INT(x)
# define RETVAL2NUM(x) INT2NUM(x)
    int num, retval = -1;
#endif
    int i;

    if (RTEST(ruby_verbose)) {
	mrb_warning(mrb_state *mrb, "We plan to remove a syscall function at future release. DL(Fiddle) provides safer alternative.");
    }

mrb_secure(mrb_state *mrb, 2);
    if (argc == 0)
	mrb_raise(mrb_state *mrb, mrb_eArgError, "too few arguments for syscall");
    if (argc > numberof(arg))
	mrb_raise(mrb_state *mrb, mrb_eArgError, "too many arguments for syscall");
    num = NUM2SYSCALLID(argv[0]); ++argv;
    for (i = argc - 1; i--; ) {
	mrb_value v = mrb_check_string_type(argv[i]);

	if (!NIL_P(v)) {
	    SafeStringValue(v);
	    mrb_str_modify(v);
	    arg[i] = (mrb_value)StringValueCStr(v);
	}
	else {
	    arg[i] = (mrb_value)NUM2LONG(argv[i]);
	}
    }

    switch (argc) {
      case 1:
	retval = SYSCALL(num);
	break;
      case 2:
	retval = SYSCALL(num, arg[0]);
	break;
      case 3:
	retval = SYSCALL(num, arg[0],arg[1]);
	break;
      case 4:
	retval = SYSCALL(num, arg[0],arg[1],arg[2]);
	break;
      case 5:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3]);
	break;
      case 6:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4]);
	break;
      case 7:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	break;
      case 8:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6]);
	break;
#ifdef atarist
      case 9:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],
	  arg[7]);
	break;
      case 10:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],
	  arg[7], arg[8]);
	break;
      case 11:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],
	  arg[7], arg[8], arg[9]);
	break;
      case 12:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],
	  arg[7], arg[8], arg[9], arg[10]);
	break;
      case 13:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],
	  arg[7], arg[8], arg[9], arg[10], arg[11]);
	break;
      case 14:
	retval = SYSCALL(num, arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],
	  arg[7], arg[8], arg[9], arg[10], arg[11], arg[12]);
        break;
#endif
    }

    if (retval == -1)
	mrb_sys_fail(mrb_state *mrb, 0);
    return RETVAL2NUM(retval);
#undef SYSCALL
#undef NUM2SYSCALLID
#undef RETVAL2NUM
}
#else
#define mrb_f_syscall mrb_f_notimplement
#endif

static mrb_value
io_new_instance(mrb_state *mrb, mrb_value args)
{
    return mrb_class_new_instance(2, (mrb_value*)args+1, *(mrb_value*)args);
}

static void
io_encoding_set(mrb_state *mrb, mrb_io_t *fptr, mrb_value v1, mrb_value v2, mrb_value opt)
{
    mrb_encoding *enc, *enc2;
    int ecflags = fptr->encs.ecflags;
    mrb_value ecopts, tmp;

    if (!NIL_P(v2)) {
	enc2 = mrb_to_encoding(v1);
	tmp = mrb_check_string_type(v2);
	if (!NIL_P(tmp)) {
	    if (RSTRING_LEN(tmp) == 1 && RSTRING_PTR(tmp)[0] == '-') {
		/* Special case - "-" => no transcoding */
		enc = enc2;
		enc2 = NULL;
	    }
	    else
		enc = mrb_to_encoding(v2);
	    if (enc == enc2) {
		/* Special case - "-" => no transcoding */
		enc2 = NULL;
	    }
	}
	else
	    enc = mrb_to_encoding(v2);
	SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(mrb_state *mrb, enc2, ecflags);
	ecflags = mrb_econv_prepare_options(opt, &ecopts, ecflags);
    }
    else {
	if (NIL_P(v1)) {
	    /* Set to default encodings */
	    mrb_io_ext_int_to_encs(NULL, NULL, &enc, &enc2);
	    SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(enc2, ecflags);
            ecopts = Qnil;
	}
	else {
	    tmp = mrb_check_string_type(v1);
	    if (!NIL_P(tmp) && mrb_enc_asciicompat(mrb_enc_get(tmp))) {
parse_mode_enc(mrb_state *mrb, RSTRING_PTR(tmp), &enc, &enc2, NULL);
		SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(mrb_state *mrb, enc2, ecflags);
                ecflags = mrb_econv_prepare_options(opt, &ecopts, ecflags);
	    }
	    else {
		mrb_io_ext_int_to_encs(mrb_state *mrb, mrb_to_encoding(v1), NULL, &enc, &enc2);
		SET_UNIVERSAL_NEWLINE_DECORATOR_IF_ENC2(mrb_state *mrb, enc2, ecflags);
                ecopts = Qnil;
	    }
	}
    }
validate_enc_binmode(mrb_state *mrb, &fptr->mode, ecflags, enc, enc2);
    fptr->encs.enc = enc;
    fptr->encs.enc2 = enc2;
    fptr->encs.ecflags = ecflags;
    fptr->encs.ecopts = ecopts;
clear_codeconv(mrb_state *mrb, fptr);

}

static mrb_value
pipe_pair_close(mrb_state *mrb, mrb_value rw)
{
    mrb_value *rwp = (mrb_value *)rw;
    return mrb_ensure(io_close, rwp[0], io_close, rwp[1]);
}

/*
 *  call-seq:
 *     IO.pipe                             ->  [read_io, write_io]
 *     IO.pipe(ext_enc)                    ->  [read_io, write_io]
 *     IO.pipe("ext_enc:int_enc" [, opt])  ->  [read_io, write_io]
 *     IO.pipe(ext_enc, int_enc [, opt])   ->  [read_io, write_io]
 *
 *     IO.pipe(...) {|read_io, write_io| ... }
 *
 *  Creates a pair of pipe endpoints (connected to each other) and
 *  returns them as a two-element array of <code>IO</code> objects:
 *  <code>[</code> <i>read_io</i>, <i>write_io</i> <code>]</code>.
 *
 *  If a block is given, the block is called and
 *  returns the value of the block.
 *  <i>read_io</i> and <i>write_io</i> are sent to the block as arguments.
 *  If read_io and write_io are not closed when the block exits, they are closed.
 *  i.e. closing read_io and/or write_io doesn't cause an error.
 *
 *  Not available on all platforms.
 *
 *  If an encoding (encoding name or encoding object) is specified as an optional argument,
 *  read string from pipe is tagged with the encoding specified.
 *  If the argument is a colon separated two encoding names "A:B",
 *  the read string is converted from encoding A (external encoding)
 *  to encoding B (internal encoding), then tagged with B.
 *  If two optional arguments are specified, those must be
 *  encoding objects or encoding names,
 *  and the first one is the external encoding,
 *  and the second one is the internal encoding.
 *  If the external encoding and the internal encoding is specified,
 *  optional hash argument specify the conversion option.
 *
 *  In the example below, the two processes close the ends of the pipe
 *  that they are not using. This is not just a cosmetic nicety. The
 *  read end of a pipe will not generate an end of file condition if
 *  there are any writers with the pipe still open. In the case of the
 *  parent process, the <code>rd.read</code> will never return if it
 *  does not first issue a <code>wr.close</code>.
 *
 *     rd, wr = IO.pipe
 *
 *     if fork
 *       wr.close
 *       puts "Parent got: <#{rd.read}>"
 *       rd.close
 *       Process.wait
 *     else
 *       rd.close
 *       puts "Sending message to parent"
 *       wr.write "Hi Dad"
 *       wr.close
 *     end
 *
 *  <em>produces:</em>
 *
 *     Sending message to parent
 *     Parent got: <Hi Dad>
 */

static mrb_value
mrb_io_s_pipe(mrb_state *mrb, int argc, mrb_value *argv, mrb_value klass)
{
    int pipes[2], state;
    mrb_value r, w, args[3], v1, v2;
    mrb_value opt;
    mrb_io_t *fptr, *fptr2;
    int fmode = 0;
    mrb_value ret;

    argc = mrb_scan_args(argc, argv, "02:", &v1, &v2, &opt);
    if (mrb_pipe(pipes) == -1)
mrb_sys_fail(mrb_state *mrb, 0);

    args[0] = klass;
    args[1] = INT2NUM(pipes[0]);
    args[2] = INT2FIX(O_RDONLY);
    r = mrb_protect(io_new_instance, (mrb_value)args, &state);
    if (state) {
	close(mrb_state *mrb, pipes[0]);
	close(mrb_state *mrb, pipes[1]);
	mrb_jump_tag(mrb_state *mrb, state);
    }
GetOpenFile(mrb_state *mrb, r, fptr);
io_encoding_set(mrb_state *mrb, fptr, v1, v2, opt);
    args[1] = INT2NUM(pipes[1]);
    args[2] = INT2FIX(O_WRONLY);
    w = mrb_protect(io_new_instance, (mrb_value)args, &state);
    if (state) {
	close(mrb_state *mrb, pipes[1]);
	if (!NIL_P(r)) mrb_io_close(r);
	mrb_jump_tag(mrb_state *mrb, state);
    }
GetOpenFile(mrb_state *mrb, w, fptr2);
mrb_io_synchronized(mrb_state *mrb, fptr2);

extract_binmode(mrb_state *mrb, opt, &fmode);
#if DEFAULT_TEXTMODE
    if ((fptr->mode & FMODE_TEXTMODE) && (fmode & FMODE_BINMODE)) {
	fptr->mode &= ~FMODE_TEXTMODE;
	setmode(mrb_state *mrb, fptr->fd, O_BINARY);
    }
#if defined(RUBY_TEST_CRLF_ENVIRONMENT) || defined(_WIN32)
    if (fptr->encs.ecflags & ECONV_DEFAULT_NEWLINE_DECORATOR) {
	fptr->encs.ecflags |= ECONV_UNIVERSAL_NEWLINE_DECORATOR;
    }
#endif
#endif
    fptr->mode |= fmode;
#if DEFAULT_TEXTMODE
    if ((fptr2->mode & FMODE_TEXTMODE) && (fmode & FMODE_BINMODE)) {
	fptr2->mode &= ~FMODE_TEXTMODE;
	setmode(mrb_state *mrb, fptr2->fd, O_BINARY);
    }
#endif
    fptr2->mode |= fmode;

    ret = mrb_assoc_new(r, w);
    if (mrb_block_given_p()) {
	mrb_value rw[2];
	rw[0] = r;
	rw[1] = w;
	return mrb_ensure(mrb_yield, ret, pipe_pair_close, (mrb_value)rw);
    }
    return ret;
}

struct foreach_arg {
    int argc;
    mrb_value *argv;
    mrb_value io;
};

static void
open_key_args(mrb_state *mrb, int argc, mrb_value *argv, mrb_value opt, struct foreach_arg *arg)
{
    mrb_value path, v;

    path = *argv++;
    argc--;
FilePathValue(mrb_state *mrb, path);
    arg->io = 0;
    arg->argc = argc;
    arg->argv = argv;
    if (NIL_P(opt)) {
	arg->io = mrb_io_open(path, INT2NUM(O_RDONLY), INT2FIX(0666), Qnil);
	return;
    }
    v = mrb_hash_aref(opt, sym_open_args);
    if (!NIL_P(v)) {
	mrb_value args;
	long n;

	v = mrb_convert_type(v, T_ARRAY, "Array", "to_ary");
	n = RARRAY_LEN(v) + 1;
#if SIZEOF_LONG > SIZEOF_INT
	if (n > INT_MAX) {
	    mrb_raise(mrb_eArgError, "too many arguments");
	}
#endif
	args = mrb_ary_tmp_new(n);
	mrb_ary_push(mrb_state *mrb, args, path);
	mrb_ary_concat(mrb_state *mrb, args, v);
	arg->io = mrb_io_open_with_args((int)n, RARRAY_PTR(args));
	mrb_ary_clear(mrb_state *mrb, args);	/* prevent from GC */
	return;
    }
    arg->io = mrb_io_open(path, Qnil, Qnil, opt);
}

static mrb_value
io_s_foreach(mrb_state *mrb, struct foreach_arg *arg)
{
    mrb_value str;

    while (!NIL_P(str = mrb_io_gets_m(arg->argc, arg->argv, arg->io))) {
	mrb_yield(mrb_state *mrb, str);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     IO.foreach(name, sep=$/ [, open_args]) {|line| block }     -> nil
 *     IO.foreach(name, limit [, open_args]) {|line| block }      -> nil
 *     IO.foreach(name, sep, limit [, open_args]) {|line| block } -> nil
 *     IO.foreach(...)                                            -> an_enumerator
 *
 *  Executes the block for every line in the named I/O port, where lines
 *  are separated by <em>sep</em>.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 *     IO.foreach("testfile") {|x| print "GOT ", x }
 *
 *  <em>produces:</em>
 *
 *     GOT This is line one
 *     GOT This is line two
 *     GOT This is line three
 *     GOT And so on...
 *
 *  If the last argument is a hash, it's the keyword argument to open.
 *  See <code>IO.read</code> for detail.
 *
 */

static mrb_value
mrb_io_s_foreach(mrb_state *mrb, int argc, mrb_value *argv, mrb_value self)
{
    mrb_value opt;
    int orig_argc = argc;
    struct foreach_arg arg;

    argc = mrb_scan_args(argc, argv, "13:", NULL, NULL, NULL, NULL, &opt);
RETURN_ENUMERATOR(mrb_state *mrb, self, orig_argc, argv);
open_key_args(mrb_state *mrb, argc, argv, opt, &arg);
    if (NIL_P(arg.io)) return Qnil;
    return mrb_ensure(io_s_foreach, (mrb_value)&arg, mrb_io_close, arg.io);
}

static mrb_value
io_s_readlines(mrb_state *mrb, struct foreach_arg *arg)
{
    return mrb_io_readlines(arg->argc, arg->argv, arg->io);
}

/*
 *  call-seq:
 *     IO.readlines(name, sep=$/ [, open_args])     -> array
 *     IO.readlines(name, limit [, open_args])      -> array
 *     IO.readlines(name, sep, limit [, open_args]) -> array
 *
 *  Reads the entire file specified by <i>name</i> as individual
 *  lines, and returns those lines in an array. Lines are separated by
 *  <i>sep</i>.
 *
 *     a = IO.readlines("testfile")
 *     a[0]   #=> "This is line one\n"
 *
 *  If the last argument is a hash, it's the keyword argument to open.
 *  See <code>IO.read</code> for detail.
 *
 */

static mrb_value
mrb_io_s_readlines(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value opt;
    struct foreach_arg arg;

    argc = mrb_scan_args(argc, argv, "13:", NULL, NULL, NULL, NULL, &opt);
open_key_args(mrb_state *mrb, argc, argv, opt, &arg);
    if (NIL_P(arg.io)) return Qnil;
    return mrb_ensure(io_s_readlines, (mrb_value)&arg, mrb_io_close, arg.io);
}

static mrb_value
io_s_read(mrb_state *mrb, struct foreach_arg *arg)
{
    return io_read(arg->argc, arg->argv, arg->io);
}

struct seek_arg {
    mrb_value io;
    mrb_value offset;
    int mode;
};

static mrb_value
seek_before_access(mrb_state *mrb, mrb_value argp)
{
    struct seek_arg *arg = (struct seek_arg *)argp;
mrb_io_binmode(mrb_state *mrb, arg->io);
    return mrb_io_seek(arg->io, arg->offset, arg->mode);
}

/*
 *  call-seq:
 *     IO.read(name, [length [, offset]] )   -> string
 *     IO.read(name, [length [, offset]], open_args)   -> string
 *
 *  Opens the file, optionally seeks to the given <i>offset</i>, then returns
 *  <i>length</i> bytes (defaulting to the rest of the file).
 *  <code>read</code> ensures the file is closed before returning.
 *
 *  If the last argument is a hash, it specifies option for internal
 *  open().  The key would be the following.  open_args: is exclusive
 *  to others.
 *
 *   encoding: string or encoding
 *
 *    specifies encoding of the read string.  encoding will be ignored
 *    if length is specified.
 *
 *   mode: string
 *
 *    specifies mode argument for open().  it should start with "r"
 *    otherwise it would cause error.
 *
 *   open_args: array of strings
 *
 *    specifies arguments for open() as an array.
 *
 *     IO.read("testfile")           #=> "This is line one\nThis is line two\nThis is line three\nAnd so on...\n"
 *     IO.read("testfile", 20)       #=> "This is line one\nThi"
 *     IO.read("testfile", 20, 10)   #=> "ne one\nThis is line "
 */

static mrb_value
mrb_io_s_read(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value opt, offset;
    struct foreach_arg arg;

    argc = mrb_scan_args(argc, argv, "13:", NULL, NULL, &offset, NULL, &opt);
open_key_args(mrb_state *mrb, argc, argv, opt, &arg);
    if (NIL_P(arg.io)) return Qnil;
    if (!NIL_P(offset)) {
	struct seek_arg sarg;
	int state = 0;
	sarg.io = arg.io;
	sarg.offset = offset;
	sarg.mode = SEEK_SET;
	mrb_protect(mrb_state *mrb, seek_before_access, (mrb_value)&sarg, &state);
	if (state) {
	    mrb_io_close(arg.io);
	    mrb_jump_tag(state);
	}
	if (arg.argc == 2) arg.argc = 1;
    }
    return mrb_ensure(io_s_read, (mrb_value)&arg, mrb_io_close, arg.io);
}

/*
 *  call-seq:
 *     IO.binread(name, [length [, offset]] )   -> string
 *
 *  Opens the file, optionally seeks to the given <i>offset</i>, then returns
 *  <i>length</i> bytes (defaulting to the rest of the file).
 *  <code>binread</code> ensures the file is closed before returning.
 *  The open mode would be "rb:ASCII-8BIT".
 *
 *     IO.binread("testfile")           #=> "This is line one\nThis is line two\nThis is line three\nAnd so on...\n"
 *     IO.binread("testfile", 20)       #=> "This is line one\nThi"
 *     IO.binread("testfile", 20, 10)   #=> "ne one\nThis is line "
 */

static mrb_value
mrb_io_s_binread(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value offset;
    struct foreach_arg arg;

mrb_scan_args(mrb_state *mrb, argc, argv, "12", NULL, NULL, &offset);
FilePathValue(mrb_state *mrb, argv[0]);
    arg.io = mrb_io_open(argv[0], mrb_str_new_cstr("rb:ASCII-8BIT"), Qnil, Qnil);
    if (NIL_P(arg.io)) return Qnil;
    arg.argv = argv+1;
    arg.argc = (argc > 1) ? 1 : 0;
    if (!NIL_P(offset)) {
	mrb_io_seek(mrb_state *mrb, arg.io, offset, SEEK_SET);
    }
    return mrb_ensure(io_s_read, (mrb_value)&arg, mrb_io_close, arg.io);
}

static mrb_value
io_s_write0(mrb_state *mrb, struct write_arg *arg)
{
    return io_write(arg->io,arg->str,arg->nosync);
}

static mrb_value
io_s_write(mrb_state *mrb, int argc, mrb_value *argv, int binary)
{
    mrb_value string, offset, opt;
    struct foreach_arg arg;
    struct write_arg warg;

mrb_scan_args(mrb_state *mrb, argc, argv, "21:", NULL, &string, &offset, &opt);

    if (NIL_P(opt)) opt = mrb_hash_new();
    else opt = mrb_hash_dup(opt);


    if (NIL_P(mrb_hash_aref(opt,sym_mode))) {
       int mode = O_WRONLY|O_CREAT;
#ifdef O_BINARY
       if (binary) mode |= O_BINARY;
#endif
       if (NIL_P(offset)) mode |= O_TRUNC;
mrb_hash_aset(mrb_state *mrb, opt,sym_mode,INT2NUM(mode));
    }
open_key_args(mrb_state *mrb, argc,argv,opt,&arg);

#ifndef O_BINARY
    if (binary) mrb_io_binmode_m(arg.io);
#endif

    if (NIL_P(arg.io)) return Qnil;
    if (!NIL_P(offset)) {
       struct seek_arg sarg;
       int state = 0;
       sarg.io = arg.io;
       sarg.offset = offset;
       sarg.mode = SEEK_SET;
mrb_protect(mrb_state *mrb, seek_before_access, (mrb_value)&sarg, &state);
       if (state) {
mrb_io_close(mrb_state *mrb, arg.io);
mrb_jump_tag(mrb_state *mrb, state);
       }
    }

    warg.io = arg.io;
    warg.str = string;
    warg.nosync = 0;

    return mrb_ensure(io_s_write0, (mrb_value)&warg, mrb_io_close, arg.io);
}

/*
 *  call-seq:
 *     IO.write(name, string, [offset] )   => fixnum
 *     IO.write(name, string, [offset], open_args )   => fixnum
 *
 *  Opens the file, optionally seeks to the given <i>offset</i>, writes
 *  <i>string</i>, then returns the length written.
 *  <code>write</code> ensures the file is closed before returning.
 *  If <i>offset</i> is not given, the file is truncated.  Otherwise,
 *  it is not truncated.
 *
 *  If the last argument is a hash, it specifies option for internal
 *  open().  The key would be the following.  open_args: is exclusive
 *  to others.
 *
 *   encoding: string or encoding
 *
 *    specifies encoding of the read string.  encoding will be ignored
 *    if length is specified.
 *
 *   mode: string
 *
 *    specifies mode argument for open().  it should start with "w" or "a" or "r+"
 *    otherwise it would cause error.
 *
 *   perm: fixnum
 *
 *    specifies perm argument for open().
 *
 *   open_args: array
 *
 *    specifies arguments for open() as an array.
 *
 *     IO.write("testfile", "0123456789", 20) # => 10
 *     # File could contain:  "This is line one\nThi0123456789two\nThis is line three\nAnd so on...\n"
 *     IO.write("testfile", "0123456789")      #=> 10
 *     # File would now read: "0123456789"
 */

static mrb_value
mrb_io_s_write(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    return io_s_write(argc, argv, 0);
}

/*
 *  call-seq:
 *     IO.binwrite(name, string, [offset] )   => fixnum
 *
 *  Opens the file, optionally seeks to the given <i>offset</i>, writes
 *  <i>string</i> then returns the length written.
 *  <code>binwrite</code> ensures the file is closed before returning.
 *  The open mode would be "wb:ASCII-8BIT".
 *  If <i>offset</i> is not given, the file is truncated.  Otherwise,
 *  it is not truncated.
 *
 *     IO.binwrite("testfile", "0123456789", 20) # => 10
 *     # File could contain:  "This is line one\nThi0123456789two\nThis is line three\nAnd so on...\n"
 *     IO.binwrite("testfile", "0123456789")      #=> 10
 *     # File would now read: "0123456789"
 */

static mrb_value
mrb_io_s_binwrite(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    return io_s_write(argc, argv, 1);
}

struct copy_stream_struct {
    mrb_value src;
    mrb_value dst;
    off_t copy_length; /* (off_t)-1 if not specified */
    off_t src_offset; /* (off_t)-1 if not specified */

    int src_fd;
    int dst_fd;
    int close_src;
    int close_dst;
    off_t total;
    const char *syserr;
    int error_no;
    const char *notimp;
    mrb_fdset_t fds;
    mrb_value th;
};

static void *
exec_interrupts(mrb_state *mrb, void *arg)
{
    mrb_value th = (mrb_value)arg;
mrb_thread_execute_interrupts(mrb_state *mrb, th);
    return NULL;
}

/*
 * returns TRUE if the preceding system call was interrupted
 * so we can continue.  If the thread was interrupted, we
 * reacquire the GVL to execute interrupts before continuing.
 */
static int
maygvl_copy_stream_continue_p(mrb_state *mrb, int has_gvl, struct copy_stream_struct *stp)
{
    switch (errno) {
      case EINTR:
#if defined(ERESTART)
      case ERESTART:
#endif
	if (mrb_thread_interrupted(stp->th)) {
            if (has_gvl)
mrb_thread_execute_interrupts(mrb_state *mrb, stp->th);
            else
mrb_thread_call_with_gvl(mrb_state *mrb, exec_interrupts, (void *)stp->th);
        }
	return TRUE;
    }
    return FALSE;
}

static int
maygvl_select(mrb_state *mrb, int has_gvl, int n, mrb_fdset_t *rfds, mrb_fdset_t *wfds, mrb_fdset_t *efds, struct timeval *timeout)
{
    if (has_gvl)
        return mrb_thread_fd_select(n, rfds, wfds, efds, timeout);
    else
        return mrb_fd_select(n, rfds, wfds, efds, timeout);
}

static int
maygvl_copy_stream_wait_read(mrb_state *mrb, int has_gvl, struct copy_stream_struct *stp)
{
    int ret;

    do {
	mrb_fd_zero(mrb_state *mrb, &stp->fds);
	mrb_fd_set(mrb_state *mrb, stp->src_fd, &stp->fds);
        ret = maygvl_select(has_gvl, mrb_fd_max(&stp->fds), &stp->fds, NULL, NULL, NULL);
    } while (ret == -1 && maygvl_copy_stream_continue_p(has_gvl, stp));

    if (ret == -1) {
        stp->syserr = "select";
        stp->error_no = errno;
        return -1;
    }
    return 0;
}

static int
nogvl_copy_stream_wait_write(mrb_state *mrb, struct copy_stream_struct *stp)
{
    int ret;

    do {
	mrb_fd_zero(mrb_state *mrb, &stp->fds);
	mrb_fd_set(mrb_state *mrb, stp->dst_fd, &stp->fds);
        ret = mrb_fd_select(mrb_fd_max(&stp->fds), NULL, &stp->fds, NULL, NULL);
    } while (ret == -1 && maygvl_copy_stream_continue_p(0, stp));

    if (ret == -1) {
        stp->syserr = "select";
        stp->error_no = errno;
        return -1;
    }
    return 0;
}

#ifdef HAVE_SENDFILE

# ifdef __linux__
#  define USE_SENDFILE

#  ifdef HAVE_SYS_SENDFILE_H
#   include <sys/sendfile.h>
#  endif

static ssize_t
simple_sendfile(mrb_state *mrb, int out_fd, int in_fd, off_t *offset, off_t count)
{
    return sendfile(out_fd, in_fd, offset, (size_t)count);
}

# elif 0 /* defined(__FreeBSD__) || defined(__DragonFly__) */ || defined(__APPLE__)
/* This runs on FreeBSD8.1 r30210, but sendfiles blocks its execution
 * without cpuset -l 0.
 */
#  define USE_SENDFILE

#  ifdef HAVE_SYS_UIO_H
#   include <sys/uio.h>
#  endif

static ssize_t
simple_sendfile(mrb_state *mrb, int out_fd, int in_fd, off_t *offset, off_t count)
{
    int r;
    off_t pos = offset ? *offset : lseek(in_fd, 0, SEEK_CUR);
    off_t sbytes;
#  ifdef __APPLE__
    r = sendfile(in_fd, out_fd, pos, &count, NULL, 0);
    sbytes = count;
#  else
    r = sendfile(in_fd, out_fd, pos, (size_t)count, NULL, &sbytes, 0);
#  endif
    if (r != 0 && sbytes == 0) return -1;
    if (offset) {
	*offset += sbytes;
    }
    else {
	lseek(mrb_state *mrb, in_fd, sbytes, SEEK_CUR);
    }
    return (ssize_t)sbytes;
}

# endif

#endif

#ifdef USE_SENDFILE
static int
nogvl_copy_stream_sendfile(mrb_state *mrb, struct copy_stream_struct *stp)
{
    struct stat src_stat, dst_stat;
    ssize_t ss;
    int ret;

    off_t copy_length;
    off_t src_offset;
    int use_pread;

    ret = fstat(stp->src_fd, &src_stat);
    if (ret == -1) {
        stp->syserr = "fstat";
        stp->error_no = errno;
        return -1;
    }
    if (!S_ISREG(src_stat.st_mode))
        return 0;

    ret = fstat(stp->dst_fd, &dst_stat);
    if (ret == -1) {
        stp->syserr = "fstat";
        stp->error_no = errno;
        return -1;
    }
    if ((dst_stat.st_mode & S_IFMT) != S_IFSOCK)
        return 0;

    src_offset = stp->src_offset;
    use_pread = src_offset != (off_t)-1;

    copy_length = stp->copy_length;
    if (copy_length == (off_t)-1) {
        if (use_pread)
            copy_length = src_stat.st_size - src_offset;
        else {
            off_t cur;
            errno = 0;
            cur = lseek(stp->src_fd, 0, SEEK_CUR);
            if (cur == (off_t)-1 && errno) {
                stp->syserr = "lseek";
                stp->error_no = errno;
                return -1;
            }
            copy_length = src_stat.st_size - cur;
        }
    }

  retry_sendfile:
# if SIZEOF_OFF_T > SIZEOF_SIZE_T
    /* we are limited by the 32-bit ssize_t return value on 32-bit */
    ss = (copy_length > (off_t)SSIZE_MAX) ? SSIZE_MAX : (ssize_t)copy_length;
# else
    ss = (ssize_t)copy_length;
# endif
    if (use_pread) {
        ss = simple_sendfile(stp->dst_fd, stp->src_fd, &src_offset, ss);
    }
    else {
        ss = simple_sendfile(stp->dst_fd, stp->src_fd, NULL, ss);
    }
    if (0 < ss) {
        stp->total += ss;
        copy_length -= ss;
        if (0 < copy_length) {
            goto retry_sendfile;
        }
    }
    if (ss == -1) {
	if (maygvl_copy_stream_continue_p(0, stp))
	    goto retry_sendfile;
        switch (errno) {
	  case EINVAL:
#ifdef ENOSYS
	  case ENOSYS:
#endif
            return 0;
	  case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
#ifndef linux
           /*
            * Linux requires stp->src_fd to be a mmap-able (regular) file,
            * select() reports regular files to always be "ready", so
            * there is no need to select() on it.
            * Other OSes may have the same limitation for sendfile() which
            * allow us to bypass maygvl_copy_stream_wait_read()...
            */
            if (maygvl_copy_stream_wait_read(0, stp) == -1)
                return -1;
#endif
            if (nogvl_copy_stream_wait_write(stp) == -1)
                return -1;
            goto retry_sendfile;
        }
        stp->syserr = "sendfile";
        stp->error_no = errno;
        return -1;
    }
    return 1;
}
#endif

static ssize_t
maygvl_read(mrb_state *mrb, int has_gvl, int fd, void *buf, size_t count)
{
    if (has_gvl)
        return mrb_read_internal(fd, buf, count);
    else
        return read(fd, buf, count);
}

static ssize_t
maygvl_copy_stream_read(mrb_state *mrb, int has_gvl, struct copy_stream_struct *stp, char *buf, size_t len, off_t offset)
{
    ssize_t ss;
  retry_read:
    if (offset == (off_t)-1) {
        ss = maygvl_read(has_gvl, stp->src_fd, buf, len);
    }
    else {
#ifdef HAVE_PREAD
        ss = pread(stp->src_fd, buf, len, offset);
#else
        stp->notimp = "pread";
        return -1;
#endif
    }
    if (ss == 0) {
        return 0;
    }
    if (ss == -1) {
	if (maygvl_copy_stream_continue_p(has_gvl, stp))
	    goto retry_read;
        switch (errno) {
	  case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
            if (maygvl_copy_stream_wait_read(has_gvl, stp) == -1)
                return -1;
            goto retry_read;
#ifdef ENOSYS
	  case ENOSYS:
#endif
            stp->notimp = "pread";
            return -1;
        }
        stp->syserr = offset == (off_t)-1 ?  "read" : "pread";
        stp->error_no = errno;
        return -1;
    }
    return ss;
}

static int
nogvl_copy_stream_write(mrb_state *mrb, struct copy_stream_struct *stp, char *buf, size_t len)
{
    ssize_t ss;
    int off = 0;
    while (len) {
        ss = write(stp->dst_fd, buf+off, len);
        if (ss == -1) {
	    if (maygvl_copy_stream_continue_p(0, stp))
		continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (nogvl_copy_stream_wait_write(stp) == -1)
                    return -1;
                continue;
            }
            stp->syserr = "write";
            stp->error_no = errno;
            return -1;
        }
        off += (int)ss;
        len -= (int)ss;
        stp->total += ss;
    }
    return 0;
}

static void
nogvl_copy_stream_read_write(mrb_state *mrb, struct copy_stream_struct *stp)
{
    char buf[1024*16];
    size_t len;
    ssize_t ss;
    int ret;
    off_t copy_length;
    int use_eof;
    off_t src_offset;
    int use_pread;

    copy_length = stp->copy_length;
    use_eof = copy_length == (off_t)-1;
    src_offset = stp->src_offset;
    use_pread = src_offset != (off_t)-1;

    if (use_pread && stp->close_src) {
        off_t r;
	errno = 0;
        r = lseek(stp->src_fd, src_offset, SEEK_SET);
        if (r == (off_t)-1 && errno) {
            stp->syserr = "lseek";
            stp->error_no = errno;
            return;
        }
        src_offset = (off_t)-1;
        use_pread = 0;
    }

    while (use_eof || 0 < copy_length) {
        if (!use_eof && copy_length < (off_t)sizeof(buf)) {
            len = (size_t)copy_length;
        }
        else {
            len = sizeof(buf);
        }
        if (use_pread) {
            ss = maygvl_copy_stream_read(0, stp, buf, len, src_offset);
            if (0 < ss)
                src_offset += ss;
        }
        else {
            ss = maygvl_copy_stream_read(0, stp, buf, len, (off_t)-1);
        }
        if (ss <= 0) /* EOF or error */
            return;

        ret = nogvl_copy_stream_write(stp, buf, ss);
        if (ret < 0)
            return;

        if (!use_eof)
            copy_length -= ss;
    }
}

static mrb_value
nogvl_copy_stream_func(mrb_state *mrb, void *arg)
{
    struct copy_stream_struct *stp = (struct copy_stream_struct *)arg;
#ifdef USE_SENDFILE
    int ret;
#endif

#ifdef USE_SENDFILE
    ret = nogvl_copy_stream_sendfile(stp);
    if (ret != 0)
        goto finish; /* error or success */
#endif

nogvl_copy_stream_read_write(mrb_state *mrb, stp);

#ifdef USE_SENDFILE
  finish:
#endif
    return Qnil;
}

static mrb_value
copy_stream_fallback_body(mrb_state *mrb, mrb_value arg)
{
    struct copy_stream_struct *stp = (struct copy_stream_struct *)arg;
    const int buflen = 16*1024;
    mrb_value n;
    mrb_value buf = mrb_str_buf_new(buflen);
    off_t rest = stp->copy_length;
    off_t off = stp->src_offset;
    ID read_method = id_readpartial;

    if (stp->src_fd == -1) {
	if (!mrb_respond_to(stp->src, read_method)) {
	    read_method = id_read;
	}
    }

    while (1) {
        long numwrote;
        long l;
        if (stp->copy_length == (off_t)-1) {
            l = buflen;
        }
        else {
            if (rest == 0)
                break;
            l = buflen < rest ? buflen : (long)rest;
        }
        if (stp->src_fd == -1) {
mrb_funcall(mrb_state *mrb, stp->src, read_method, 2, INT2FIX(l), buf);
        }
        else {
            ssize_t ss;
mrb_thread_wait_fd(mrb_state *mrb, stp->src_fd);
mrb_str_resize(mrb_state *mrb, buf, buflen);
            ss = maygvl_copy_stream_read(1, stp, RSTRING_PTR(buf), l, off);
            if (ss == -1)
                return Qnil;
            if (ss == 0)
mrb_eof_error(mrb_state *mrb, );
mrb_str_resize(mrb_state *mrb, buf, ss);
            if (off != (off_t)-1)
                off += ss;
        }
        n = mrb_io_write(stp->dst, buf);
        numwrote = NUM2LONG(n);
        stp->total += numwrote;
        rest -= numwrote;
	if (read_method == id_read && RSTRING_LEN(buf) == 0) {
	    break;
	}
    }

    return Qnil;
}

static mrb_value
copy_stream_fallback(mrb_state *mrb, struct copy_stream_struct *stp)
{
    if (stp->src_fd == -1 && stp->src_offset != (off_t)-1) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "cannot specify src_offset for non-IO");
    }
mrb_rescue2(mrb_state *mrb, copy_stream_fallback_body, (mrb_value)stp,
(mrb_state *mrb, mrb_value (*) (ANYARGS))0, (mrb_value)0,
               mrb_eEOFError, (mrb_value)0);
    return Qnil;
}

static mrb_value
copy_stream_body(mrb_state *mrb, mrb_value arg)
{
    struct copy_stream_struct *stp = (struct copy_stream_struct *)arg;
    mrb_value src_io, dst_io;
    mrb_io_t *src_fptr = 0, *dst_fptr = 0;
    int src_fd, dst_fd;

    stp->th = mrb_thread_current();

    stp->total = 0;

    if (stp->src == argf ||
!(mrb_state *mrb, TYPE(stp->src) == T_FILE ||
TYPE(mrb_state *mrb, stp->src) == T_STRING ||
mrb_respond_to(mrb_state *mrb, stp->src, mrb_intern("to_path")))) {
        src_fd = -1;
    }
    else {
        src_io = TYPE(stp->src) == T_FILE ? stp->src : Qnil;
        if (NIL_P(src_io)) {
            mrb_value args[2];
            int oflags = O_RDONLY;
#ifdef O_NOCTTY
            oflags |= O_NOCTTY;
#endif
FilePathValue(mrb_state *mrb, stp->src);
            args[0] = stp->src;
            args[1] = INT2NUM(oflags);
            src_io = mrb_class_new_instance(2, args, mrb_cFile);
            stp->src = src_io;
            stp->close_src = 1;
        }
GetOpenFile(mrb_state *mrb, src_io, src_fptr);
mrb_io_check_byte_readable(mrb_state *mrb, src_fptr);
        src_fd = src_fptr->fd;
    }
    stp->src_fd = src_fd;

    if (stp->dst == argf ||
!(mrb_state *mrb, TYPE(stp->dst) == T_FILE ||
TYPE(mrb_state *mrb, stp->dst) == T_STRING ||
mrb_respond_to(mrb_state *mrb, stp->dst, mrb_intern("to_path")))) {
        dst_fd = -1;
    }
    else {
        dst_io = TYPE(stp->dst) == T_FILE ? stp->dst : Qnil;
        if (NIL_P(dst_io)) {
            mrb_value args[3];
            int oflags = O_WRONLY|O_CREAT|O_TRUNC;
#ifdef O_NOCTTY
            oflags |= O_NOCTTY;
#endif
FilePathValue(mrb_state *mrb, stp->dst);
            args[0] = stp->dst;
            args[1] = INT2NUM(oflags);
            args[2] = INT2FIX(0600);
            dst_io = mrb_class_new_instance(3, args, mrb_cFile);
            stp->dst = dst_io;
            stp->close_dst = 1;
        }
        else {
            dst_io = GetWriteIO(dst_io);
            stp->dst = dst_io;
        }
GetOpenFile(mrb_state *mrb, dst_io, dst_fptr);
mrb_io_check_writable(mrb_state *mrb, dst_fptr);
        dst_fd = dst_fptr->fd;
    }
    stp->dst_fd = dst_fd;

#ifdef O_BINARY
    if (src_fptr)
	SET_BINARY_MODE_WITH_SEEK_CUR(mrb_state *mrb, src_fptr);
    if (dst_fptr)
	setmode(mrb_state *mrb, dst_fd, O_BINARY);
#endif

    if (stp->src_offset == (off_t)-1 && src_fptr && src_fptr->rbuf.len) {
        size_t len = src_fptr->rbuf.len;
        mrb_value str;
        if (stp->copy_length != (off_t)-1 && stp->copy_length < (off_t)len) {
            len = (size_t)stp->copy_length;
        }
        str = mrb_str_buf_new(len);
mrb_str_resize(mrb_state *mrb, str,len);
read_buffered_data(mrb_state *mrb, RSTRING_PTR(str), len, src_fptr);
        if (dst_fptr) { /* IO or filename */
            if (io_binwrite(str, RSTRING_PTR(str), RSTRING_LEN(str), dst_fptr, 0) < 0)
mrb_sys_fail(mrb_state *mrb, 0);
        }
        else /* others such as StringIO */
mrb_io_write(mrb_state *mrb, stp->dst, str);
        stp->total += len;
        if (stp->copy_length != (off_t)-1)
            stp->copy_length -= len;
    }

    if (dst_fptr && io_fflush(dst_fptr) < 0) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "flush failed");
    }

    if (stp->copy_length == 0)
        return Qnil;

    if (src_fd == -1 || dst_fd == -1) {
        return copy_stream_fallback(stp);
    }

mrb_fd_set(mrb_state *mrb, src_fd, &stp->fds);
mrb_fd_set(mrb_state *mrb, dst_fd, &stp->fds);

    return mrb_thread_blocking_region(nogvl_copy_stream_func, (void*)stp, RUBY_UBF_IO, 0);
}

static mrb_value
copy_stream_finalize(mrb_state *mrb, mrb_value arg)
{
    struct copy_stream_struct *stp = (struct copy_stream_struct *)arg;
    if (stp->close_src) {
mrb_io_close_m(mrb_state *mrb, stp->src);
    }
    if (stp->close_dst) {
mrb_io_close_m(mrb_state *mrb, stp->dst);
    }
mrb_fd_term(mrb_state *mrb, &stp->fds);
    if (stp->syserr) {
        errno = stp->error_no;
mrb_sys_fail(mrb_state *mrb, stp->syserr);
    }
    if (stp->notimp) {
	mrb_raise(mrb_state *mrb, mrb_eNotImpError, "%s() not implemented", stp->notimp);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     IO.copy_stream(src, dst)
 *     IO.copy_stream(src, dst, copy_length)
 *     IO.copy_stream(src, dst, copy_length, src_offset)
 *
 *  IO.copy_stream copies <i>src</i> to <i>dst</i>.
 *  <i>src</i> and <i>dst</i> is either a filename or an IO.
 *
 *  This method returns the number of bytes copied.
 *
 *  If optional arguments are not given,
 *  the start position of the copy is
 *  the beginning of the filename or
 *  the current file offset of the IO.
 *  The end position of the copy is the end of file.
 *
 *  If <i>copy_length</i> is given,
 *  No more than <i>copy_length</i> bytes are copied.
 *
 *  If <i>src_offset</i> is given,
 *  it specifies the start position of the copy.
 *
 *  When <i>src_offset</i> is specified and
 *  <i>src</i> is an IO,
 *  IO.copy_stream doesn't move the current file offset.
 *
 */
static mrb_value
mrb_io_s_copy_stream(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_value src, dst, length, src_offset;
    struct copy_stream_struct st;

MEMZERO(mrb_state *mrb, &st, struct copy_stream_struct, 1);

mrb_scan_args(mrb_state *mrb, argc, argv, "22", &src, &dst, &length, &src_offset);

    st.src = src;
    st.dst = dst;

    if (NIL_P(length))
        st.copy_length = (off_t)-1;
    else
        st.copy_length = NUM2OFFT(length);

    if (NIL_P(src_offset))
        st.src_offset = (off_t)-1;
    else
        st.src_offset = NUM2OFFT(src_offset);

mrb_fd_init(mrb_state *mrb, &st.fds);
mrb_ensure(mrb_state *mrb, copy_stream_body, (mrb_value)&st, copy_stream_finalize, (mrb_value)&st);

    return OFFT2NUM(st.total);
}

/*
 *  call-seq:
 *     io.external_encoding   -> encoding
 *
 *  Returns the Encoding object that represents the encoding of the file.
 *  If io is write mode and no encoding is specified, returns <code>nil</code>.
 */

static mrb_value
mrb_io_external_encoding(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
    if (fptr->encs.enc2) {
	return mrb_enc_from_encoding(fptr->encs.enc2);
    }
    if (fptr->mode & FMODE_WRITABLE) {
	if (fptr->encs.enc)
	    return mrb_enc_from_encoding(fptr->encs.enc);
	return Qnil;
    }
    return mrb_enc_from_encoding(io_read_encoding(fptr));
}

/*
 *  call-seq:
 *     io.internal_encoding   -> encoding
 *
 *  Returns the Encoding of the internal string if conversion is
 *  specified.  Otherwise returns nil.
 */

static mrb_value
mrb_io_internal_encoding(mrb_state *mrb, mrb_value io)
{
    mrb_io_t *fptr;

GetOpenFile(mrb_state *mrb, io, fptr);
    if (!fptr->encs.enc2) return Qnil;
    return mrb_enc_from_encoding(io_read_encoding(fptr));
}

/*
 *  call-seq:
 *     io.set_encoding(ext_enc)                -> io
 *     io.set_encoding("ext_enc:int_enc")      -> io
 *     io.set_encoding(ext_enc, int_enc)       -> io
 *     io.set_encoding("ext_enc:int_enc", opt) -> io
 *     io.set_encoding(ext_enc, int_enc, opt)  -> io
 *
 *  If single argument is specified, read string from io is tagged
 *  with the encoding specified.  If encoding is a colon separated two
 *  encoding names "A:B", the read string is converted from encoding A
 *  (external encoding) to encoding B (internal encoding), then tagged
 *  with B.  If two arguments are specified, those must be encoding
 *  objects or encoding names, and the first one is the external encoding, and the
 *  second one is the internal encoding.
 *  If the external encoding and the internal encoding is specified,
 *  optional hash argument specify the conversion option.
 */

static mrb_value
mrb_io_set_encoding(mrb_state *mrb, int argc, mrb_value *argv, mrb_value io)
{
    mrb_io_t *fptr;
    mrb_value v1, v2, opt;

    if (TYPE(io) != T_FILE) {
        return mrb_funcall2(io, id_set_encoding, argc, argv);
    }

    argc = mrb_scan_args(argc, argv, "11:", &v1, &v2, &opt);
GetOpenFile(mrb_state *mrb, io, fptr);
io_encoding_set(mrb_state *mrb, fptr, v1, v2, opt);
    return io;
}

void
mrb_stdio_set_default_encoding(mrb_state *mrb, void)
{
    extern mrb_value mrb_stdin, mrb_stdout, mrb_stderr;
    mrb_value val = Qnil;

mrb_io_set_encoding(mrb_state *mrb, 1, &val, mrb_stdin);
mrb_io_set_encoding(mrb_state *mrb, 1, &val, mrb_stdout);
mrb_io_set_encoding(mrb_state *mrb, 1, &val, mrb_stderr);
}

/*
 *  call-seq:
 *     ARGF.external_encoding   -> encoding
 *
 *  Returns the external encoding for files read from +ARGF+ as an +Encoding+
 *  object. The external encoding is the encoding of the text as stored in a
 *  file. Contrast with +ARGF.internal_encoding+, which is the encoding used
 *  to represent this text within Ruby.
 *
 *  To set the external encoding use +ARGF.set_encoding+.
 *
 * For example:
 *
 *     ARGF.external_encoding  #=>  #<Encoding:UTF-8>
 *
 */
static mrb_value
argf_external_encoding(mrb_state *mrb, mrb_value argf)
{
    if (!RTEST(ARGF.current_file)) {
	return mrb_enc_from_encoding(mrb_default_external_encoding());
    }
    return mrb_io_external_encoding(mrb_io_check_io(ARGF.current_file));
}

/*
 *  call-seq:
 *     ARGF.internal_encoding   -> encoding
 *
 *  Returns the internal encoding for strings read from +ARGF+ as an
 *  +Encoding+ object.
 *
 *  If +ARGF.set_encoding+ has been called with two encoding names, the second
 *  is returned. Otherwise, if +Encoding.default_external+ has been set, that
 *  value is returned. Failing that, if a default external encoding was
 *  specified on the command-line, that value is used. If the encoding is
 *  unknown, nil is returned.
 */
static mrb_value
argf_internal_encoding(mrb_state *mrb, mrb_value argf)
{
    if (!RTEST(ARGF.current_file)) {
	return mrb_enc_from_encoding(mrb_default_external_encoding());
    }
    return mrb_io_internal_encoding(mrb_io_check_io(ARGF.current_file));
}

/*
 *  call-seq:
 *     ARGF.set_encoding(ext_enc)                -> ARGF
 *     ARGF.set_encoding("ext_enc:int_enc")      -> ARGF
 *     ARGF.set_encoding(ext_enc, int_enc)       -> ARGF
 *     ARGF.set_encoding("ext_enc:int_enc", opt) -> ARGF
 *     ARGF.set_encoding(ext_enc, int_enc, opt)  -> ARGF
 *
 *  If single argument is specified, strings read from ARGF are tagged with
 *  the encoding specified.
 *
 *  If two encoding names separated by a colon are given, e.g. "ascii:utf-8",
 *  the read string is converted from the first encoding (external encoding)
 *  to the second encoding (internal encoding), then tagged with the second
 *  encoding.
 *
 *  If two arguments are specified, they must be encoding objects or encoding
 *  names. Again, the first specifies the external encoding; the second
 *  specifies the internal encoding.
 *
 *  If the external encoding and the internal encoding are specified, the
 *  optional +Hash+ argument can be used to adjust the conversion process. The
 *  structure of this hash is explained in the +String#encode+ documentation.
 *
 *  For example:
 *
 *      ARGF.set_encoding('ascii')         # Tag the input as US-ASCII text
 *      ARGF.set_encoding(Encoding::UTF_8) # Tag the input as UTF-8 text
 *      ARGF.set_encoding('utf-8','ascii') # Transcode the input from US-ASCII
 *                                         # to UTF-8.
 */
static mrb_value
argf_set_encoding(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    mrb_io_t *fptr;

    if (!next_argv()) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "no stream to set encoding");
    }
mrb_io_set_encoding(mrb_state *mrb, argc, argv, ARGF.current_file);
GetOpenFile(mrb_state *mrb, ARGF.current_file, fptr);
    ARGF.encs = fptr->encs;
    return argf;
}

/*
 *  call-seq:
 *     ARGF.tell  -> Integer
 *     ARGF.pos   -> Integer
 *
 *  Returns the current offset (in bytes) of the current file in +ARGF+.
 *
 *     ARGF.pos    #=> 0
 *     ARGF.gets   #=> "This is line one\n"
 *     ARGF.pos    #=> 17
 *
 */
static mrb_value
argf_tell(mrb_state *mrb, mrb_value argf)
{
    if (!next_argv()) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "no stream to tell");
    }
ARGF_FORWARD(mrb_state *mrb, 0, 0);
    return mrb_io_tell(ARGF.current_file);
}

/*
 *  call-seq:
 *     ARGF.seek(amount, whence=IO::SEEK_SET)  ->  0
 *
 *  Seeks to offset _amount_ (an +Integer+) in the +ARGF+ stream according to
 *  the value of _whence_. See +IO#seek+ for further details.
 */
static mrb_value
argf_seek_m(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    if (!next_argv()) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "no stream to seek");
    }
ARGF_FORWARD(mrb_state *mrb, argc, argv);
    return mrb_io_seek_m(argc, argv, ARGF.current_file);
}

/*
 *  call-seq:
 *     ARGF.pos = position  -> Integer
 *
 *  Seeks to the position given by _position_ (in bytes) in +ARGF+.
 *
 *  For example:
 *
 *      ARGF.pos = 17
 *      ARGF.gets   #=> "This is line two\n"
 */
static mrb_value
argf_set_pos(mrb_state *mrb, mrb_value argf, mrb_value offset)
{
    if (!next_argv()) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "no stream to set position");
    }
ARGF_FORWARD(mrb_state *mrb, 1, &offset);
    return mrb_io_set_pos(ARGF.current_file, offset);
}

/*
 *  call-seq:
 *     ARGF.rewind   -> 0
 *
 *  Positions the current file to the beginning of input, resetting
 *  +ARGF.lineno+ to zero.
 *
 *     ARGF.readline   #=> "This is line one\n"
 *     ARGF.rewind     #=> 0
 *     ARGF.lineno     #=> 0
 *     ARGF.readline   #=> "This is line one\n"
 */
static mrb_value
argf_rewind(mrb_state *mrb, mrb_value argf)
{
    if (!next_argv()) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "no stream to rewind");
    }
ARGF_FORWARD(mrb_state *mrb, 0, 0);
    return mrb_io_rewind(ARGF.current_file);
}

/*
 *  call-seq:
 *     ARGF.fileno    -> fixnum
 *     ARGF.to_i      -> fixnum
 *
 *  Returns an integer representing the numeric file descriptor for
 *  the current file. Raises an +ArgumentError+ if there isn't a current file.
 *
 *     ARGF.fileno    #=> 3
 */
static mrb_value
argf_fileno(mrb_state *mrb, mrb_value argf)
{
    if (!next_argv()) {
	mrb_raise(mrb_state *mrb, mrb_eArgError, "no stream");
    }
ARGF_FORWARD(mrb_state *mrb, 0, 0);
    return mrb_io_fileno(ARGF.current_file);
}

/*
 *  call-seq:
 *     ARGF.to_io     -> IO
 *
 *  Returns an +IO+ object representing the current file. This will be a
 *  +File+ object unless the current file is a stream such as STDIN.
 *
 *  For example:
 *
 *     ARGF.to_io    #=> #<File:glark.txt>
 *     ARGF.to_io    #=> #<IO:<STDIN>>
 */
static mrb_value
argf_to_io(mrb_state *mrb, mrb_value argf)
{
next_argv(mrb_state *mrb, );
ARGF_FORWARD(mrb_state *mrb, 0, 0);
    return ARGF.current_file;
}

/*
 *  call-seq:
 *     ARGF.eof?  -> true or false
 *     ARGF.eof   -> true or false
 *
 *  Returns true if the current file in +ARGF+ is at end of file, i.e. it has
 *  no data to read. The stream must be opened for reading or an +IOError+
 *  will be raised.
 *
 *     $ echo "eof" | ruby argf.rb
 *
 *     ARGF.eof?                 #=> false
 *     3.times { ARGF.readchar }
 *     ARGF.eof?                 #=> false
 *     ARGF.readchar             #=> "\n"
 *     ARGF.eof?                 #=> true
 */

static mrb_value
argf_eof(mrb_state *mrb, mrb_value argf)
{
next_argv(mrb_state *mrb, );
    if (RTEST(ARGF.current_file)) {
	if (ARGF.init_p == 0) return Qtrue;
	next_argv(mrb_state *mrb, );
	ARGF_FORWARD(mrb_state *mrb, 0, 0);
	if (mrb_io_eof(ARGF.current_file)) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

/*
 *  call-seq:
 *     ARGF.read([length [, buffer]])    -> string, buffer, or nil
 *
 *  Reads _length_ bytes from ARGF. The files named on the command line
 *  are concatenated and treated as a single file by this method, so when
 *  called without arguments the contents of this pseudo file are returned in
 *  their entirety.
 *
 *  _length_ must be a non-negative integer or nil. If it is a positive
 *  integer, +read+ tries to read at most _length_ bytes. It returns nil
 *  if an EOF was encountered before anything could be read. Fewer than
 *  _length_ bytes may be returned if an EOF is encountered during the read.
 *
 *  If _length_ is omitted or is _nil_, it reads until EOF. A String is
 *  returned even if EOF is encountered before any data is read.
 *
 *  If _length_ is zero, it returns _""_.
 *
 *  If the optional _buffer_ argument is present, it must reference a String,
 *  which will receive the data.
 *
 * For example:
 *
 *     $ echo "small" > small.txt
 *     $ echo "large" > large.txt
 *     $ ./glark.rb small.txt large.txt
 *
 *     ARGF.read      #=> "small\nlarge"
 *     ARGF.read(200) #=> "small\nlarge"
 *     ARGF.read(2)   #=> "sm"
 *     ARGF.read(0)   #=> ""
 *
 *  Note that this method behaves like fread() function in C.  If you need the
 *  behavior like read(2) system call, consider +ARGF.readpartial+.
 */

static mrb_value
argf_read(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    mrb_value tmp, str, length;
    long len = 0;

mrb_scan_args(mrb_state *mrb, argc, argv, "02", &length, &str);
    if (!NIL_P(length)) {
	len = NUM2LONG(argv[0]);
    }
    if (!NIL_P(str)) {
	StringValue(mrb_state *mrb, str);
	mrb_str_resize(mrb_state *mrb, str,0);
	argv[1] = Qnil;
    }

  retry:
    if (!next_argv()) {
	return str;
    }
    if (ARGF_GENERIC_INPUT_P()) {
	tmp = argf_forward(argc, argv, argf);
    }
    else {
	tmp = io_read(argc, argv, ARGF.current_file);
    }
    if (NIL_P(str)) str = tmp;
    else if (!NIL_P(tmp)) mrb_str_append(str, tmp);
    if (NIL_P(tmp) || NIL_P(length)) {
	if (ARGF.next_p != -1) {
	    argf_close(ARGF.current_file);
	    ARGF.next_p = 1;
	    goto retry;
	}
    }
    else if (argc >= 1) {
	if (RSTRING_LEN(str) < len) {
	    len -= RSTRING_LEN(str);
	    argv[0] = INT2NUM(len);
	    goto retry;
	}
    }
    return str;
}

struct argf_call_arg {
    int argc;
    mrb_value *argv;
    mrb_value argf;
};

static mrb_value
argf_forward_call(mrb_state *mrb, mrb_value arg)
{
    struct argf_call_arg *p = (struct argf_call_arg *)arg;
argf_forward(mrb_state *mrb, p->argc, p->argv, p->argf);
    return Qnil;
}

static mrb_value argf_getpartial(int argc, mrb_value *argv, mrb_value argf, int nonblock);

/*
 *  call-seq:
 *     ARGF.readpartial(maxlen)              -> string
 *     ARGF.readpartial(maxlen, outbuf)      -> outbuf
 *
 *  Reads at most _maxlen_ bytes from the ARGF stream. It blocks only if
 *  +ARGF+ has no data immediately available. If the optional _outbuf_
 *  argument is present, it must reference a String, which will receive the
 *  data. It raises <code>EOFError</code> on end of file.
 *
 *  +readpartial+ is designed for streams such as pipes, sockets, and ttys. It
 *  blocks only when no data is immediately available. This means that it
 *  blocks only when following all conditions hold:
 *
 *  * The byte buffer in the +IO+ object is empty.
 *  * The content of the stream is empty.
 *  * The stream has not reached EOF.
 *
 *  When +readpartial+ blocks, it waits for data or EOF. If some data is read,
 *  +readpartial+ returns with the data. If EOF is reached, readpartial raises
 *  an +EOFError+.
 *
 *  When +readpartial+ doesn't block, it returns or raises immediately.  If
 *  the byte buffer is not empty, it returns the data in the buffer. Otherwise, if
 *  the stream has some content, it returns the data in the stream. If the
 *  stream reaches EOF an +EOFError+ is raised.
 */

static mrb_value
argf_readpartial(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    return argf_getpartial(argc, argv, argf, 0);
}

/*
 *  call-seq:
 *     ARGF.read_nonblock(maxlen)              -> string
 *     ARGF.read_nonblock(maxlen, outbuf)      -> outbuf
 *
 *  Reads at most _maxlen_ bytes from the ARGF stream in non-blocking mode.
 */

static mrb_value
argf_read_nonblock(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
    return argf_getpartial(argc, argv, argf, 1);
}

static mrb_value
argf_getpartial(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf, int nonblock)
{
    mrb_value tmp, str, length;

mrb_scan_args(mrb_state *mrb, argc, argv, "11", &length, &str);
    if (!NIL_P(str)) {
StringValue(mrb_state *mrb, str);
        argv[1] = str;
    }

    if (!next_argv()) {
mrb_str_resize(mrb_state *mrb, str, 0);
mrb_eof_error(mrb_state *mrb, );
    }
    if (ARGF_GENERIC_INPUT_P()) {
	struct argf_call_arg arg;
	arg.argc = argc;
	arg.argv = argv;
	arg.argf = argf;
	tmp = mrb_rescue2(argf_forward_call, (mrb_value)&arg,
			 RUBY_METHOD_FUNC(0), Qnil, mrb_eEOFError, (mrb_value)0);
    }
    else {
        tmp = io_getpartial(argc, argv, ARGF.current_file, nonblock);
    }
    if (NIL_P(tmp)) {
        if (ARGF.next_p == -1) {
mrb_eof_error(mrb_state *mrb, );
        }
argf_close(mrb_state *mrb, ARGF.current_file);
        ARGF.next_p = 1;
        if (RARRAY_LEN(ARGF.argv) == 0)
mrb_eof_error(mrb_state *mrb, );
        if (NIL_P(str))
            str = mrb_str_new(NULL, 0);
        return str;
    }
    return tmp;
}

/*
 *  call-seq:
 *     ARGF.getc  -> String or nil
 *
 *  Reads the next character from +ARGF+ and returns it as a +String+. Returns
 *  +nil+ at the end of the stream.
 *
 *  +ARGF+ treats the files named on the command line as a single file created
 *  by concatenating their contents. After returning the last character of the
 *  first file, it returns the first character of the second file, and so on.
 *
 *  For example:
 *
 *     $ echo "foo" > file
 *     $ ruby argf.rb file
 *
 *     ARGF.getc  #=> "f"
 *     ARGF.getc  #=> "o"
 *     ARGF.getc  #=> "o"
 *     ARGF.getc  #=> "\n"
 *     ARGF.getc  #=> nil
 *     ARGF.getc  #=> nil
 */
static mrb_value
argf_getc(mrb_state *mrb, mrb_value argf)
{
    mrb_value ch;

  retry:
    if (!next_argv()) return Qnil;
    if (ARGF_GENERIC_INPUT_P()) {
	ch = mrb_funcall3(ARGF.current_file, mrb_intern("getc"), 0, 0);
    }
    else {
	ch = mrb_io_getc(ARGF.current_file);
    }
    if (NIL_P(ch) && ARGF.next_p != -1) {
	argf_close(mrb_state *mrb, ARGF.current_file);
	ARGF.next_p = 1;
	goto retry;
    }

    return ch;
}

/*
 *  call-seq:
 *     ARGF.getbyte  -> Fixnum or nil
 *
 *  Gets the next 8-bit byte (0..255) from +ARGF+. Returns +nil+ if called at
 *  the end of the stream.
 *
 *  For example:
 *
 *     $ echo "foo" > file
 *     $ ruby argf.rb file
 *
 *     ARGF.getbyte #=> 102
 *     ARGF.getbyte #=> 111
 *     ARGF.getbyte #=> 111
 *     ARGF.getbyte #=> 10
 *     ARGF.getbyte #=> nil
 */
static mrb_value
argf_getbyte(mrb_state *mrb, mrb_value argf)
{
    mrb_value ch;

  retry:
    if (!next_argv()) return Qnil;
    if (TYPE(ARGF.current_file) != T_FILE) {
	ch = mrb_funcall3(ARGF.current_file, mrb_intern("getbyte"), 0, 0);
    }
    else {
	ch = mrb_io_getbyte(ARGF.current_file);
    }
    if (NIL_P(ch) && ARGF.next_p != -1) {
	argf_close(mrb_state *mrb, ARGF.current_file);
	ARGF.next_p = 1;
	goto retry;
    }

    return ch;
}

/*
 *  call-seq:
 *     ARGF.readchar  -> String or nil
 *
 *  Reads the next character from +ARGF+ and returns it as a +String+. Raises
 *  an +EOFError+ after the last character of the last file has been read.
 *
 *  For example:
 *
 *     $ echo "foo" > file
 *     $ ruby argf.rb file
 *
 *     ARGF.readchar  #=> "f"
 *     ARGF.readchar  #=> "o"
 *     ARGF.readchar  #=> "o"
 *     ARGF.readchar  #=> "\n"
 *     ARGF.readchar  #=> end of file reached (EOFError)
 */
static mrb_value
argf_readchar(mrb_state *mrb, mrb_value argf)
{
    mrb_value ch;

  retry:
    if (!next_argv()) mrb_eof_error();
    if (TYPE(ARGF.current_file) != T_FILE) {
	ch = mrb_funcall3(ARGF.current_file, mrb_intern("getc"), 0, 0);
    }
    else {
	ch = mrb_io_getc(ARGF.current_file);
    }
    if (NIL_P(ch) && ARGF.next_p != -1) {
	argf_close(mrb_state *mrb, ARGF.current_file);
	ARGF.next_p = 1;
	goto retry;
    }

    return ch;
}

/*
 *  call-seq:
 *     ARGF.readbyte  -> Fixnum
 *
 *  Reads the next 8-bit byte from ARGF and returns it as a +Fixnum+. Raises
 *  an +EOFError+ after the last byte of the last file has been read.
 *
 *  For example:
 *
 *     $ echo "foo" > file
 *     $ ruby argf.rb file
 *
 *     ARGF.readbyte  #=> 102
 *     ARGF.readbyte  #=> 111
 *     ARGF.readbyte  #=> 111
 *     ARGF.readbyte  #=> 10
 *     ARGF.readbyte  #=> end of file reached (EOFError)
 */
static mrb_value
argf_readbyte(mrb_state *mrb, mrb_value argf)
{
    mrb_value c;

NEXT_ARGF_FORWARD(mrb_state *mrb, 0, 0);
    c = argf_getbyte(argf);
    if (NIL_P(c)) {
	mrb_eof_error(mrb_state *mrb, );
    }
    return c;
}

/*
 *  call-seq:
 *     ARGF.each(sep=$/)            {|line| block }  -> ARGF
 *     ARGF.each(sep=$/,limit)      {|line| block }  -> ARGF
 *     ARGF.each(...)                                -> an_enumerator
 *
 *     ARGF.each_line(sep=$/)       {|line| block }  -> ARGF
 *     ARGF.each_line(sep=$/,limit) {|line| block }  -> ARGF
 *     ARGF.each_line(...)                           -> an_enumerator
 *
 *     ARGF.lines(sep=$/)           {|line| block }   -> ARGF
 *     ARGF.lines(sep=$/,limit)     {|line| block }   -> ARGF
 *     ARGF.lines(...)                                -> an_enumerator
 *
 *  Returns an enumerator which iterates over each line (separated by _sep_,
 *  which defaults to your platform's newline character) of each file in
 *  +ARGV+. If a block is supplied, each line in turn will be yielded to the
 *  block, otherwise an enumerator is returned.
 *  The optional _limit_ argument is a +Fixnum+ specifying the maximum
 *  length of each line; longer lines will be split according to this limit.
 *
 *  This method allows you to treat the files supplied on the command line as
 *  a single file consisting of the concatenation of each named file. After
 *  the last line of the first file has been returned, the first line of the
 *  second file is returned. The +ARGF.filename+ and +ARGF.lineno+ methods can
 *  be used to determine the filename and line number, respectively, of the
 *  current line.
 *
 *  For example, the following code prints out each line of each named file
 *  prefixed with its line number, displaying the filename once per file:
 *
 *     ARGF.lines do |line|
 *       puts ARGF.filename if ARGF.lineno == 1
 *       puts "#{ARGF.lineno}: #{line}"
 *     end
 */
static mrb_value
argf_each_line(mrb_state *mrb, int argc, mrb_value *argv, mrb_value argf)
{
RETURN_ENUMERATOR(mrb_state *mrb, argf, argc, argv);
    for (;;) {
	if (!next_argv()) return argf;
	mrb_block_call(mrb_state *mrb, ARGF.current_file, mrb_intern("each_line"), argc, argv, 0, 0);
	ARGF.next_p = 1;
    }
}

/*
 *  call-seq:
 *     ARGF.bytes     {|byte| block }  -> ARGF
 *     ARGF.bytes                      -> an_enumerator
 *
 *     ARGF.each_byte {|byte| block }  -> ARGF
 *     ARGF.each_byte                  -> an_enumerator
 *
 *  Iterates over each byte of each file in +ARGV+.
 *  A byte is returned as a +Fixnum+ in the range 0..255.
 *
 *  This method allows you to treat the files supplied on the command line as
 *  a single file consisting of the concatenation of each named file. After
 *  the last byte of the first file has been returned, the first byte of the
 *  second file is returned. The +ARGF.filename+ method can be used to
 *  determine the filename of the current byte.
 *
 *  If no block is given, an enumerator is returned instead.
 *
 * For example:
 *
 *     ARGF.bytes.to_a  #=> [35, 32, ... 95, 10]
 *
 */
static mrb_value
argf_each_byte(mrb_state *mrb, mrb_value argf)
{
RETURN_ENUMERATOR(mrb_state *mrb, argf, 0, 0);
    for (;;) {
	if (!next_argv()) return argf;
	mrb_block_call(mrb_state *mrb, ARGF.current_file, mrb_intern("each_byte"), 0, 0, 0, 0);
	ARGF.next_p = 1;
    }
}

/*
 *  call-seq:
 *     ARGF.chars      {|char| block }  -> ARGF
 *     ARGF.chars                       -> an_enumerator
 *
 *     ARGF.each_char  {|char| block }  -> ARGF
 *     ARGF.each_char                   -> an_enumerator
 *
 *  Iterates over each character of each file in +ARGF+.
 *
 *  This method allows you to treat the files supplied on the command line as
 *  a single file consisting of the concatenation of each named file. After
 *  the last character of the first file has been returned, the first
 *  character of the second file is returned. The +ARGF.filename+ method can
 *  be used to determine the name of the file in which the current character
 *  appears.
 *
 *  If no block is given, an enumerator is returned instead.
 */
static mrb_value
argf_each_char(mrb_state *mrb, mrb_value argf)
{
RETURN_ENUMERATOR(mrb_state *mrb, argf, 0, 0);
    for (;;) {
	if (!next_argv()) return argf;
	mrb_block_call(mrb_state *mrb, ARGF.current_file, mrb_intern("each_char"), 0, 0, 0, 0);
	ARGF.next_p = 1;
    }
}

/*
 *  call-seq:
 *     ARGF.filename  -> String
 *     ARGF.path      -> String
 *
 *  Returns the current filename. "-" is returned when the current file is
 *  STDIN.
 *
 *  For example:
 *
 *     $ echo "foo" > foo
 *     $ echo "bar" > bar
 *     $ echo "glark" > glark
 *
 *     $ ruby argf.rb foo bar glark
 *
 *     ARGF.filename  #=> "foo"
 *     ARGF.read(5)   #=> "foo\nb"
 *     ARGF.filename  #=> "bar"
 *     ARGF.skip
 *     ARGF.filename  #=> "glark"
 */
static mrb_value
argf_filename(mrb_state *mrb, mrb_value argf)
{
next_argv(mrb_state *mrb, );
    return ARGF.filename;
}

static mrb_value
argf_filename_getter(mrb_state *mrb, ID id, mrb_value *var)
{
    return argf_filename(*var);
}

/*
 *  call-seq:
 *     ARGF.file  -> IO or File object
 *
 *  Returns the current file as an +IO+ or +File+ object. #<IO:<STDIN>> is
 *  returned when the current file is STDIN.
 *
 *  For example:
 *
 *     $ echo "foo" > foo
 *     $ echo "bar" > bar
 *
 *     $ ruby argf.rb foo bar
 *
 *     ARGF.file      #=> #<File:foo>
 *     ARGF.read(5)   #=> "foo\nb"
 *     ARGF.file      #=> #<File:bar>
 */
static mrb_value
argf_file(mrb_state *mrb, mrb_value argf)
{
next_argv(mrb_state *mrb, );
    return ARGF.current_file;
}

/*
 *  call-seq:
 *     ARGF.binmode  -> ARGF
 *
 *  Puts +ARGF+ into binary mode. Once a stream is in binary mode, it cannot
 *  be reset to non-binary mode. This option has the following effects:
 *
 *  *  Newline conversion is disabled.
 *  *  Encoding conversion is disabled.
 *  *  Content is treated as ASCII-8BIT.
 */
static mrb_value
argf_binmode_m(mrb_state *mrb, mrb_value argf)
{
    ARGF.binmode = 1;
next_argv(mrb_state *mrb, );
ARGF_FORWARD(mrb_state *mrb, 0, 0);
mrb_io_ascii8bit_binmode(mrb_state *mrb, ARGF.current_file);
    return argf;
}

/*
 *  call-seq:
 *     ARGF.binmode?  -> true or false
 *
 *  Returns true if +ARGF+ is being read in binary mode; false otherwise. (To
 *  enable binary mode use +ARGF.binmode+.
 *
 * For example:
 *
 *     ARGF.binmode?  #=> false
 *     ARGF.binmode
 *     ARGF.binmode?  #=> true
 */
static mrb_value
argf_binmode_p(mrb_state *mrb, mrb_value argf)
{
    return ARGF.binmode ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     ARGF.skip  -> ARGF
 *
 *  Sets the current file to the next file in ARGV. If there aren't any more
 *  files it has no effect.
 *
 * For example:
 *
 *     $ ruby argf.rb foo bar
 *     ARGF.filename  #=> "foo"
 *     ARGF.skip
 *     ARGF.filename  #=> "bar"
 */
static mrb_value
argf_skip(mrb_state *mrb, mrb_value argf)
{
    if (ARGF.init_p && ARGF.next_p == 0) {
	argf_close(mrb_state *mrb, ARGF.current_file);
	ARGF.next_p = 1;
    }
    return argf;
}

/*
 *  call-seq:
 *     ARGF.close  -> ARGF
 *
 *  Closes the current file and skips to the next in the stream. Trying to
 *  close a file that has already been closed causes an +IOError+ to be
 *  raised.
 *
 * For example:
 *
 *     $ ruby argf.rb foo bar
 *
 *     ARGF.filename  #=> "foo"
 *     ARGF.close
 *     ARGF.filename  #=> "bar"
 *     ARGF.close
 *     ARGF.close     #=> closed stream (IOError)
 */
static mrb_value
argf_close_m(mrb_state *mrb, mrb_value argf)
{
next_argv(mrb_state *mrb, );
argf_close(mrb_state *mrb, ARGF.current_file);
    if (ARGF.next_p != -1) {
	ARGF.next_p = 1;
    }
    ARGF.lineno = 0;
    return argf;
}

/*
 *  call-seq:
 *     ARGF.closed?  -> true or false
 *
 *  Returns _true_ if the current file has been closed; _false_ otherwise. Use
 *  +ARGF.close+ to actually close the current file.
 */
static mrb_value
argf_closed(mrb_state *mrb, mrb_value argf)
{
next_argv(mrb_state *mrb, );
ARGF_FORWARD(mrb_state *mrb, 0, 0);
    return mrb_io_closed(ARGF.current_file);
}

/*
 *  call-seq:
 *     ARGF.to_s  -> String
 *
 *  Returns "ARGF".
 */
static mrb_value
argf_to_s(mrb_state *mrb, mrb_value argf)
{
    return mrb_str_new2("ARGF");
}

/*
 *  call-seq:
 *     ARGF.inplace_mode  -> String
 *
 *  Returns the file extension appended to the names of modified files under
 *  inplace-edit mode. This value can be set using +ARGF.inplace_mode=+ or
 *  passing the +-i+ switch to the Ruby binary.
 */
static mrb_value
argf_inplace_mode_get(mrb_state *mrb, mrb_value argf)
{
    if (!ARGF.inplace) return Qnil;
    return mrb_str_new2(ARGF.inplace);
}

static mrb_value
opt_i_get(mrb_state *mrb, ID id, mrb_value *var)
{
    return argf_inplace_mode_get(*var);
}

/*
 *  call-seq:
 *     ARGF.inplace_mode = ext  -> ARGF
 *
 *  Sets the filename extension for inplace editing mode to the given String.
 *  Each file being edited has this value appended to its filename. The
 *  modified file is saved under this new name.
 *
 *  For example:
 *
 *      $ ruby argf.rb file.txt
 *
 *      ARGF.inplace_mode = '.bak'
 *      ARGF.lines do |line|
 *        print line.sub("foo","bar")
 *      end
 *
 * Each line of _file.txt_ has the first occurrence of "foo" replaced with
 * "bar", then the new line is written out to _file.txt.bak_.
 */
static mrb_value
argf_inplace_mode_set(mrb_state *mrb, mrb_value argf, mrb_value val)
{
    if (mrb_safe_level() >= 1 && OBJ_TAINTED(val))
	mrb_insecure_operation(mrb_state *mrb, );

    if (!RTEST(val)) {
	if (ARGF.inplace) free(ARGF.inplace);
	ARGF.inplace = 0;
    }
    else {
	StringValue(mrb_state *mrb, val);
	if (ARGF.inplace) free(ARGF.inplace);
	ARGF.inplace = 0;
	ARGF.inplace = strdup(RSTRING_PTR(val));
    }
    return argf;
}

static void
opt_i_set(mrb_state *mrb, mrb_value val, ID id, mrb_value *var)
{
argf_inplace_mode_set(mrb_state *mrb, *var, val);
}

const char *
ruby_get_inplace_mode(mrb_state *mrb, void)
{
    return ARGF.inplace;
}

void
ruby_set_inplace_mode(mrb_state *mrb, const char *suffix)
{
    if (ARGF.inplace) free(ARGF.inplace);
    ARGF.inplace = 0;
    if (suffix) ARGF.inplace = strdup(suffix);
}

/*
 *  call-seq:
 *     ARGF.argv  -> ARGV
 *
 *  Returns the +ARGV+ array, which contains the arguments passed to your
 *  script, one per element.
 *
 *  For example:
 *
 *      $ ruby argf.rb -v glark.txt
 *
 *      ARGF.argv   #=> ["-v", "glark.txt"]
 *
 */
static mrb_value
argf_argv(mrb_state *mrb, mrb_value argf)
{
    return ARGF.argv;
}

static mrb_value
argf_argv_getter(mrb_state *mrb, ID id, mrb_value *var)
{
    return argf_argv(*var);
}

mrb_value
mrb_get_argv(mrb_state *mrb, void)
{
    return ARGF.argv;
}

/*
 *  call-seq:
 *     ARGF.to_write_io  -> io
 *
 *  Returns IO instance tied to _ARGF_ for writing if inplace mode is
 *  enabled.
 */
static mrb_value
argf_write_io(mrb_state *mrb, mrb_value argf)
{
    if (!RTEST(ARGF.current_file)) {
	mrb_raise(mrb_state *mrb, mrb_eIOError, "not opened for writing");
    }
    return GetWriteIO(ARGF.current_file);
}

/*
 *  call-seq:
 *     ARGF.write(string)   -> integer
 *
 *  Writes _string_ if inplace mode.
 */
static mrb_value
argf_write(mrb_state *mrb, mrb_value argf, mrb_value str)
{
    return mrb_io_write(argf_write_io(argf), str);
}

/*
 * Document-class: IOError
 *
 * Raised when an IO operation fails.
 *
 *    File.open("/etc/hosts") {|f| f << "example"}
 *      #=> IOError: not opened for writing
 *
 *    File.open("/etc/hosts") {|f| f.close; f.read }
 *      #=> IOError: closed stream
 *
 * Note that some IO failures raise +SystemCallError+s and these are not
 * subclasses of IOError:
 *
 *    File.open("does/not/exist")
 *      #=> Errno::ENOENT: No such file or directory - does/not/exist
 */

/*
 * Document-class: EOFError
 *
 * Raised by some IO operations when reaching the end of file. Many IO
 * methods exist in two forms,
 *
 * one that returns +nil+ when the end of file is reached, the other
 * raises EOFError +EOFError+.
 *
 * +EOFError+ is a subclass of +IOError+.
 *
 *    file = File.open("/etc/hosts")
 *    file.read
 *    file.gets     #=> nil
 *    file.readline #=> EOFError: end of file reached
 */

/*
 * Document-class:  ARGF
 *
 * +ARGF+ is a stream designed for use in scripts that process files given as
 * command-line arguments or passed in via STDIN.
 *
 * The arguments passed to your script are stored in the +ARGV+ Array, one
 * argument per element. +ARGF+ assumes that any arguments that aren't
 * filenames have been removed from +ARGV+. For example:
 *
 *     $ ruby argf.rb --verbose file1 file2
 *
 *     ARGV  #=> ["--verbose", "file1", "file2"]
 *     option = ARGV.shift #=> "--verbose"
 *     ARGV  #=> ["file1", "file2"]
 *
 * You can now use +ARGF+ to work with a concatenation of each of these named
 * files. For instance, +ARGF.read+ will return the contents of _file1_
 * followed by the contents of _file2_.
 *
 * After a file in +ARGV+ has been read +ARGF+ removes it from the Array.
 * Thus, after all files have been read +ARGV+ will be empty.
 *
 * You can manipulate +ARGV+ yourself to control what +ARGF+ operates on. If
 * you remove a file from +ARGV+, it is ignored by +ARGF+; if you add files to
 * +ARGV+, they are treated as if they were named on the command line. For
 * example:
 *
 *     ARGV.replace ["file1"]
 *     ARGF.readlines # Returns the contents of file1 as an Array
 *     ARGV           #=> []
 *     ARGV.replace ["file2", "file3"]
 *     ARGF.read      # Returns the contents of file2 and file3
 *
 * If +ARGV+ is empty, +ARGF+ acts as if it contained STDIN, i.e. the data
 * piped to your script. For example:
 *
 *     $ echo "glark" | ruby -e 'p ARGF.read'
 *     "glark\n"
 */

/*
 *  Class <code>IO</code> is the basis for all input and output in Ruby.
 *  An I/O stream may be <em>duplexed</em> (that is, bidirectional), and
 *  so may use more than one native operating system stream.
 *
 *  Many of the examples in this section use class <code>File</code>,
 *  the only standard subclass of <code>IO</code>. The two classes are
 *  closely associated.
 *
 *  As used in this section, <em>portname</em> may take any of the
 *  following forms.
 *
 *  * A plain string represents a filename suitable for the underlying
 *    operating system.
 *
 *  * A string starting with ``<code>|</code>'' indicates a subprocess.
 *    The remainder of the string following the ``<code>|</code>'' is
 *    invoked as a process with appropriate input/output channels
 *    connected to it.
 *
 *  * A string equal to ``<code>|-</code>'' will create another Ruby
 *    instance as a subprocess.
 *
 *  Ruby will convert pathnames between different operating system
 *  conventions if possible. For instance, on a Windows system the
 *  filename ``<code>/gumby/ruby/test.rb</code>'' will be opened as
 *  ``<code>\gumby\ruby\test.rb</code>''. When specifying a
 *  Windows-style filename in a Ruby string, remember to escape the
 *  backslashes:
 *
 *     "c:\\gumby\\ruby\\test.rb"
 *
 *  Our examples here will use the Unix-style forward slashes;
 *  <code>File::SEPARATOR</code> can be used to get the
 *  platform-specific separator character.
 *
 *  I/O ports may be opened in any one of several different modes, which
 *  are shown in this section as <em>mode</em>. The mode may
 *  either be a Fixnum or a String. If numeric, it should be
 *  one of the operating system specific constants (O_RDONLY,
 *  O_WRONLY, O_RDWR, O_APPEND and so on). See man open(2) for
 *  more information.
 *
 *  If the mode is given as a String, it must be one of the
 *  values listed in the following table.
 *
 *    Mode |  Meaning
 *    -----+--------------------------------------------------------
 *    "r"  |  Read-only, starts at beginning of file  (default mode).
 *    -----+--------------------------------------------------------
 *    "r+" |  Read-write, starts at beginning of file.
 *    -----+--------------------------------------------------------
 *    "w"  |  Write-only, truncates existing file
 *         |  to zero length or creates a new file for writing.
 *    -----+--------------------------------------------------------
 *    "w+" |  Read-write, truncates existing file to zero length
 *         |  or creates a new file for reading and writing.
 *    -----+--------------------------------------------------------
 *    "a"  |  Write-only, starts at end of file if file exists,
 *         |  otherwise creates a new file for writing.
 *    -----+--------------------------------------------------------
 *    "a+" |  Read-write, starts at end of file if file exists,
 *         |  otherwise creates a new file for reading and
 *         |  writing.
 *    -----+--------------------------------------------------------
 *     "b" |  Binary file mode (may appear with
 *         |  any of the key letters listed above).
 *         |  Suppresses EOL <-> CRLF conversion on Windows. And
 *         |  sets external encoding to ASCII-8BIT unless explicitly
 *         |  specified.
 *    -----+--------------------------------------------------------
 *     "t" |  Text file mode (may appear with
 *         |  any of the key letters listed above except "b").
 *
 *
 *  The global constant ARGF (also accessible as $<) provides an
 *  IO-like stream which allows access to all files mentioned on the
 *  command line (or STDIN if no files are mentioned). ARGF provides
 *  the methods <code>#path</code> and <code>#filename</code> to access
 *  the name of the file currently being read.
 *
 *  == io/console
 *
 *  The io/console extension provides methods for interacting with the
 *  console.  The console can be accessed from <code>IO.console</code> or
 *  the standard input/output/error IO objects.
 *
 *  Requiring io/console adds the following methods:
 *
 *  * IO::console
 *  * IO#raw
 *  * IO#raw!
 *  * IO#cooked
 *  * IO#cooked!
 *  * IO#getch
 *  * IO#echo=
 *  * IO#echo?
 *  * IO#noecho
 *  * IO#winsize
 *  * IO#winsize=
 *  * IO#iflush
 *  * IO#ioflush
 *  * IO#oflush
 *
 *  Example:
 *
 *    require 'io/console'
 *    rows, columns = $stdin.winsize
 *    puts "You screen is #{columns} wide and #{rows} tall"
 */

void
Init_IO(mrb_state *mrb, void)
{
#undef mrb_intern
#define mrb_intern(str) mrb_intern_const(str)

    mrb_value mrb_cARGF;
#ifdef __CYGWIN__
#include <sys/cygwin.h>
    static struct __cygwin_perfile pf[] =
    {
	{"", O_RDONLY | O_BINARY},
	{"", O_WRONLY | O_BINARY},
	{"", O_RDWR | O_BINARY},
	{"", O_APPEND | O_BINARY},
	{NULL, 0}
    };
cygwin_internal(mrb_state *mrb, CW_PERFILE, pf);
#endif

    mrb_eIOError = mrb_define_class("IOError", mrb_eStandardError);
    mrb_eEOFError = mrb_define_class("EOFError", mrb_eIOError);

    id_write = mrb_intern("write");
    id_read = mrb_intern("read");
    id_getc = mrb_intern("getc");
    id_flush = mrb_intern("flush");
    id_readpartial = mrb_intern("readpartial");
    id_set_encoding = mrb_intern("set_encoding");

mrb_define_global_function(mrb_state *mrb, "syscall", mrb_f_syscall, -1);

mrb_define_global_function(mrb_state *mrb, "open", mrb_f_open, -1);
mrb_define_global_function(mrb_state *mrb, "printf", mrb_f_printf, -1);
mrb_define_global_function(mrb_state *mrb, "print", mrb_f_print, -1);
mrb_define_global_function(mrb_state *mrb, "putc", mrb_f_putc, 1);
mrb_define_global_function(mrb_state *mrb, "puts", mrb_f_puts, -1);
mrb_define_global_function(mrb_state *mrb, "gets", mrb_f_gets, -1);
mrb_define_global_function(mrb_state *mrb, "readline", mrb_f_readline, -1);
mrb_define_global_function(mrb_state *mrb, "select", mrb_f_select, -1);

mrb_define_global_function(mrb_state *mrb, "readlines", mrb_f_readlines, -1);

mrb_define_global_function(mrb_state *mrb, "`", mrb_f_backquote, 1);

mrb_define_global_function(mrb_state *mrb, "p", mrb_f_p, -1);
mrb_define_method(mrb_state *mrb, mrb_mKernel, "display", mrb_obj_display, -1);

    mrb_cIO = mrb_define_class("IO", mrb_cObject);
mrb_include_module(mrb_state *mrb, mrb_cIO, mrb_mEnumerable);

    mrb_mWaitReadable = mrb_define_module_under(mrb_cIO, "WaitReadable");
    mrb_mWaitWritable = mrb_define_module_under(mrb_cIO, "WaitWritable");

#if 0
    /* This is necessary only for forcing rdoc handle File::open */
mrb_define_singleton_method(mrb_state *mrb, mrb_cFile, "open",  mrb_io_s_open, -1);
#endif

mrb_define_alloc_func(mrb_state *mrb, mrb_cIO, io_alloc);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "new", mrb_io_s_new, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "open",  mrb_io_s_open, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "sysopen",  mrb_io_s_sysopen, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "for_fd", mrb_io_s_for_fd, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "popen", mrb_io_s_popen, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "foreach", mrb_io_s_foreach, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "readlines", mrb_io_s_readlines, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "read", mrb_io_s_read, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "binread", mrb_io_s_binread, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "write", mrb_io_s_write, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "binwrite", mrb_io_s_binwrite, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "select", mrb_f_select, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "pipe", mrb_io_s_pipe, -1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "try_convert", mrb_io_s_try_convert, 1);
mrb_define_singleton_method(mrb_state *mrb, mrb_cIO, "copy_stream", mrb_io_s_copy_stream, -1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "initialize", mrb_io_initialize, -1);

    mrb_output_fs = Qnil;
mrb_define_hooked_variable(mrb_state *mrb, "$,", &mrb_output_fs, 0, mrb_str_setter);

    mrb_rs = mrb_default_rs = mrb_usascii_str_new2("\n");
mrb_gc_register_mark_object(mrb_state *mrb, mrb_default_rs);
    mrb_output_rs = Qnil;
OBJ_FREEZE(mrb_state *mrb, mrb_default_rs);	/* avoid modifying RS_default */
mrb_define_hooked_variable(mrb_state *mrb, "$/", &mrb_rs, 0, mrb_str_setter);
mrb_define_hooked_variable(mrb_state *mrb, "$-0", &mrb_rs, 0, mrb_str_setter);
mrb_define_hooked_variable(mrb_state *mrb, "$\\", &mrb_output_rs, 0, mrb_str_setter);

mrb_define_virtual_variable(mrb_state *mrb, "$_", mrb_lastline_get, mrb_lastline_set);

mrb_define_method(mrb_state *mrb, mrb_cIO, "initialize_copy", mrb_io_init_copy, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "reopen", mrb_io_reopen, -1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "print", mrb_io_print, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "putc", mrb_io_putc, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "puts", mrb_io_puts, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "printf", mrb_io_printf, -1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "each",  mrb_io_each_line, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "each_line",  mrb_io_each_line, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "each_byte",  mrb_io_each_byte, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "each_char",  mrb_io_each_char, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "each_codepoint",  mrb_io_each_codepoint, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "lines",  mrb_io_each_line, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "bytes",  mrb_io_each_byte, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "chars",  mrb_io_each_char, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "codepoints",  mrb_io_each_codepoint, 0);

mrb_define_method(mrb_state *mrb, mrb_cIO, "syswrite", mrb_io_syswrite, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "sysread",  mrb_io_sysread, -1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "fileno", mrb_io_fileno, 0);
mrb_define_alias(mrb_state *mrb, mrb_cIO, "to_i", "fileno");
mrb_define_method(mrb_state *mrb, mrb_cIO, "to_io", mrb_io_to_io, 0);

mrb_define_method(mrb_state *mrb, mrb_cIO, "fsync",   mrb_io_fsync, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "fdatasync",   mrb_io_fdatasync, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "sync",   mrb_io_sync, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "sync=",  mrb_io_set_sync, 1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "lineno",   mrb_io_lineno, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "lineno=",  mrb_io_set_lineno, 1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "readlines",  mrb_io_readlines, -1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "read_nonblock",  io_read_nonblock, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "write_nonblock", mrb_io_write_nonblock, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "readpartial",  io_readpartial, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "read",  io_read, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "write", io_write_m, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "gets",  mrb_io_gets_m, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "readline",  mrb_io_readline, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "getc",  mrb_io_getc, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "getbyte",  mrb_io_getbyte, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "readchar",  mrb_io_readchar, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "readbyte",  mrb_io_readbyte, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "ungetbyte",mrb_io_ungetbyte, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "ungetc",mrb_io_ungetc, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "<<",    mrb_io_addstr, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "flush", mrb_io_flush, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "tell", mrb_io_tell, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "seek", mrb_io_seek_m, -1);
mrb_define_const(mrb_state *mrb, mrb_cIO, "SEEK_SET", INT2FIX(SEEK_SET));
mrb_define_const(mrb_state *mrb, mrb_cIO, "SEEK_CUR", INT2FIX(SEEK_CUR));
mrb_define_const(mrb_state *mrb, mrb_cIO, "SEEK_END", INT2FIX(SEEK_END));
mrb_define_method(mrb_state *mrb, mrb_cIO, "rewind", mrb_io_rewind, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "pos", mrb_io_tell, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "pos=", mrb_io_set_pos, 1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "eof", mrb_io_eof, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "eof?", mrb_io_eof, 0);

mrb_define_method(mrb_state *mrb, mrb_cIO, "close_on_exec?", mrb_io_close_on_exec_p, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "close_on_exec=", mrb_io_set_close_on_exec, 1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "close", mrb_io_close_m, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "closed?", mrb_io_closed, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "close_read", mrb_io_close_read, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "close_write", mrb_io_close_write, 0);

mrb_define_method(mrb_state *mrb, mrb_cIO, "isatty", mrb_io_isatty, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "tty?", mrb_io_isatty, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "binmode",  mrb_io_binmode_m, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "binmode?", mrb_io_binmode_p, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "sysseek", mrb_io_sysseek, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "advise", mrb_io_advise, -1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "ioctl", mrb_io_ioctl, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "fcntl", mrb_io_fcntl, -1);
mrb_define_method(mrb_state *mrb, mrb_cIO, "pid", mrb_io_pid, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "inspect",  mrb_io_inspect, 0);

mrb_define_method(mrb_state *mrb, mrb_cIO, "external_encoding", mrb_io_external_encoding, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "internal_encoding", mrb_io_internal_encoding, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "set_encoding", mrb_io_set_encoding, -1);

mrb_define_method(mrb_state *mrb, mrb_cIO, "autoclose?", mrb_io_autoclose_p, 0);
mrb_define_method(mrb_state *mrb, mrb_cIO, "autoclose=", mrb_io_set_autoclose, 1);

mrb_define_variable(mrb_state *mrb, "$stdin", &mrb_stdin);
    mrb_stdin = prep_stdio(stdin, FMODE_READABLE, mrb_cIO, "<STDIN>");
mrb_define_hooked_variable(mrb_state *mrb, "$stdout", &mrb_stdout, 0, stdout_setter);
    mrb_stdout = prep_stdio(stdout, FMODE_WRITABLE, mrb_cIO, "<STDOUT>");
mrb_define_hooked_variable(mrb_state *mrb, "$stderr", &mrb_stderr, 0, stdout_setter);
    mrb_stderr = prep_stdio(stderr, FMODE_WRITABLE|FMODE_SYNC, mrb_cIO, "<STDERR>");
mrb_define_hooked_variable(mrb_state *mrb, "$>", &mrb_stdout, 0, stdout_setter);
    orig_stdout = mrb_stdout;
    mrb_deferr = orig_stderr = mrb_stderr;

    /* Holds the original stdin */
mrb_define_global_const(mrb_state *mrb, "STDIN", mrb_stdin);
    /* Holds the original stdout */
mrb_define_global_const(mrb_state *mrb, "STDOUT", mrb_stdout);
    /* Holds the original stderr */
mrb_define_global_const(mrb_state *mrb, "STDERR", mrb_stderr);

#if 0
    /* Hack to get rdoc to regard ARGF as a class: */
    mrb_cARGF = mrb_define_class("ARGF", mrb_cObject);
#endif

    mrb_cARGF = mrb_class_new(mrb_cObject);
mrb_set_class_path(mrb_state *mrb, mrb_cARGF, mrb_cObject, "ARGF.class");
mrb_define_alloc_func(mrb_state *mrb, mrb_cARGF, argf_alloc);

mrb_include_module(mrb_state *mrb, mrb_cARGF, mrb_mEnumerable);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "initialize", argf_initialize, -2);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "initialize_copy", argf_initialize_copy, 1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "to_s", argf_to_s, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "argv", argf_argv, 0);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "fileno", argf_fileno, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "to_i", argf_fileno, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "to_io", argf_to_io, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "to_write_io", argf_write_io, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "each",  argf_each_line, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "each_line",  argf_each_line, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "each_byte",  argf_each_byte, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "each_char",  argf_each_char, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "lines", argf_each_line, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "bytes", argf_each_byte, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "chars", argf_each_char, 0);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "read",  argf_read, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "readpartial",  argf_readpartial, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "read_nonblock",  argf_read_nonblock, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "readlines", argf_readlines, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "to_a", argf_readlines, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "gets", argf_gets, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "readline", argf_readline, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "getc", argf_getc, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "getbyte", argf_getbyte, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "readchar", argf_readchar, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "readbyte", argf_readbyte, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "tell", argf_tell, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "seek", argf_seek_m, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "rewind", argf_rewind, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "pos", argf_tell, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "pos=", argf_set_pos, 1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "eof", argf_eof, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "eof?", argf_eof, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "binmode", argf_binmode_m, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "binmode?", argf_binmode_p, 0);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "write", argf_write, 1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "print", mrb_io_print, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "putc", mrb_io_putc, 1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "puts", mrb_io_puts, -1);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "printf", mrb_io_printf, -1);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "filename", argf_filename, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "path", argf_filename, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "file", argf_file, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "skip", argf_skip, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "close", argf_close_m, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "closed?", argf_closed, 0);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "lineno",   argf_lineno, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "lineno=",  argf_set_lineno, 1);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "inplace_mode", argf_inplace_mode_get, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "inplace_mode=", argf_inplace_mode_set, 1);

mrb_define_method(mrb_state *mrb, mrb_cARGF, "external_encoding", argf_external_encoding, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "internal_encoding", argf_internal_encoding, 0);
mrb_define_method(mrb_state *mrb, mrb_cARGF, "set_encoding", argf_set_encoding, -1);

    argf = mrb_class_new_instance(0, 0, mrb_cARGF);

mrb_define_readonly_variable(mrb_state *mrb, "$<", &argf);
    /*
     * ARGF is a stream designed for use in scripts that process files given
     * as command-line arguments or passed in via STDIN.
     *
     * See ARGF (the class) for more details.
     */
mrb_define_global_const(mrb_state *mrb, "ARGF", argf);

mrb_define_hooked_variable(mrb_state *mrb, "$.", &argf, argf_lineno_getter, argf_lineno_setter);
mrb_define_hooked_variable(mrb_state *mrb, "$FILENAME", &argf, argf_filename_getter, mrb_gvar_readonly_setter);
    ARGF.filename = mrb_str_new2("-");

mrb_define_hooked_variable(mrb_state *mrb, "$-i", &argf, opt_i_get, opt_i_set);
mrb_define_hooked_variable(mrb_state *mrb, "$*", &argf, argf_argv_getter, mrb_gvar_readonly_setter);

#if defined (_WIN32) || defined(__CYGWIN__)
atexit(mrb_state *mrb, pipe_atexit);
#endif

Init_File(mrb_state *mrb, );

mrb_define_method(mrb_state *mrb, mrb_cFile, "initialize",  mrb_file_initialize, -1);

    /* open for reading only */
mrb_file_const(mrb_state *mrb, "RDONLY", INT2FIX(O_RDONLY));
    /* open for writing only */
mrb_file_const(mrb_state *mrb, "WRONLY", INT2FIX(O_WRONLY));
    /* open for reading and writing */
mrb_file_const(mrb_state *mrb, "RDWR", INT2FIX(O_RDWR));
    /* append on each write */
mrb_file_const(mrb_state *mrb, "APPEND", INT2FIX(O_APPEND));
    /* create file if it does not exist */
mrb_file_const(mrb_state *mrb, "CREAT", INT2FIX(O_CREAT));
    /* error if CREAT and the file exists */
mrb_file_const(mrb_state *mrb, "EXCL", INT2FIX(O_EXCL));
#if defined(O_NDELAY) || defined(O_NONBLOCK)
# ifndef O_NONBLOCK
#   define O_NONBLOCK O_NDELAY
# endif
    /* do not block on open or for data to become available */
mrb_file_const(mrb_state *mrb, "NONBLOCK", INT2FIX(O_NONBLOCK));
#endif
    /* truncate size to 0 */
mrb_file_const(mrb_state *mrb, "TRUNC", INT2FIX(O_TRUNC));
#ifdef O_NOCTTY
    /* not to make opened IO the controlling terminal device */
mrb_file_const(mrb_state *mrb, "NOCTTY", INT2FIX(O_NOCTTY));
#endif
#ifndef O_BINARY
# define  O_BINARY 0
#endif
    /* disable line code conversion and make ASCII-8BIT */
mrb_file_const(mrb_state *mrb, "BINARY", INT2FIX(O_BINARY));
#ifdef O_SYNC
mrb_file_const(mrb_state *mrb, "SYNC", INT2FIX(O_SYNC));
#endif
#ifdef O_DSYNC
mrb_file_const(mrb_state *mrb, "DSYNC", INT2FIX(O_DSYNC));
#endif
#ifdef O_RSYNC
mrb_file_const(mrb_state *mrb, "RSYNC", INT2FIX(O_RSYNC));
#endif
#ifdef O_NOFOLLOW
    /* do not follow symlinks */
mrb_file_const(mrb_state *mrb, "NOFOLLOW", INT2FIX(O_NOFOLLOW)); /* FreeBSD, Linux */
#endif
#ifdef O_NOATIME
    /* do not change atime */
mrb_file_const(mrb_state *mrb, "NOATIME", INT2FIX(O_NOATIME)); /* Linux */
#endif
#ifdef O_DIRECT
    /*  Try to minimize cache effects of the I/O to and from this file. */
mrb_file_const(mrb_state *mrb, "DIRECT", INT2FIX(O_DIRECT));
#endif

    sym_mode = ID2SYM(mrb_intern("mode"));
    sym_perm = ID2SYM(mrb_intern("perm"));
    sym_extenc = ID2SYM(mrb_intern("external_encoding"));
    sym_intenc = ID2SYM(mrb_intern("internal_encoding"));
    sym_encoding = ID2SYM(mrb_intern("encoding"));
    sym_open_args = ID2SYM(mrb_intern("open_args"));
    sym_textmode = ID2SYM(mrb_intern("textmode"));
    sym_binmode = ID2SYM(mrb_intern("binmode"));
    sym_autoclose = ID2SYM(mrb_intern("autoclose"));
    sym_normal = ID2SYM(mrb_intern("normal"));
    sym_sequential = ID2SYM(mrb_intern("sequential"));
    sym_random = ID2SYM(mrb_intern("random"));
    sym_willneed = ID2SYM(mrb_intern("willneed"));
    sym_dontneed = ID2SYM(mrb_intern("dontneed"));
    sym_noreuse = ID2SYM(mrb_intern("noreuse"));
}
