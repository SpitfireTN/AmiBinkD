/*
 *  sys.h -- 1) very system dependent functions and macros
 *           2) include for <unistd.h> or <io.h>
 *           3) defines for u8, u16, u32
 *
 *  sys.h is a part of binkd project
 *
 *  Copyright (C) 1996  Dima Maloff, 5047/13
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version. See COPYING.
 */

#ifndef _sys_h
#define _sys_h

#ifdef HAVE_INTTYPES_H
  #include <inttypes.h>
#endif
#ifdef HAVE_STDINT_H
  #include <stdint.h>
#endif
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#ifdef HAVE_IO_H
  #include <io.h>
#endif
#ifdef HAVE_DOS_H
  #include <dos.h>
#endif
#ifdef HAVE_STDARG_H
  #include <stdarg.h>
#endif
#include <stdio.h>   // FILE
#include <errno.h>   // EWOULDBLOCK etc.
#ifdef AMIGA
/* See amiga/touch.c: platform-wide guard on the datestamp call. */
extern int amiga_set_file_dates;
#endif
#include <fcntl.h>   // O_BINARY, O_NOINHERIT
#include <sys/types.h> // off_t (at least on EMX)

#if defined(__WATCOMC__) && !defined(__IBMC__)
  #include <utils.h>
  #ifdef __UTILS_32H /* toolkit 4.x or less */
    #pragma library("so32dll.lib")
    #pragma library("tcp32dll.lib")
  #else /* using toolkit 5.x */
    #define HAVE_ARPA_INET_H
    #define __IBMC__ 0
    #define __IBMCPP__ 0
  #endif
#endif

#ifdef WIN32
  #ifdef IPV6
    #if _WIN32_WINNT < 0x0502
      #define _WIN32_WINNT 0x0502            /* WinXP SP2 contains RFC2553 */
    #endif
    #define _WINSOCKAPI_                     /* do NOT include winsock.h from windows.h */
  #endif
  #include <windows.h>                       /* windows.h MUST be before winsock2.h */
  #ifdef IPV6
    #include <winsock2.h>
    #include <ws2tcpip.h>
  #else
    #include <winsock.h>
    #undef AF_INET6                         /* Winsock 1 cannot support IPv6 */
  #endif
#endif

#ifdef OS2
  #define INCL_DOS
  #define INCL_ERRORS
  #include <os2.h>
  #include <process.h>
#endif

#ifdef HAVE_THREADS
  #ifdef WITH_PTHREADS
    #include <pthread.h>
    #ifdef HAVE_GETTID
      #include <sys/syscall.h>
      #define PID() (int)syscall(SYS_gettid)
    #else
      #define PID() ((int)(0xffff & (long int)pthread_self()))
    #endif
  #else
    #include <process.h>
    #if defined(OS2)
      int gettid (void);
      #define PID() gettid()
    #elif defined(WIN32)
      #define PID() ((int)(0xffff & GetCurrentThreadId()))
    #else
      #define PID() ((int)gettid())
    #endif
  #endif
#elif defined(__MSC__) || defined(DJGPP)
  #include <process.h>
  #define PID()    ((int)getpid())
  void dos_sleep(int);
  #define sleep(s) dos_sleep(s)
#elif defined(AMIGA)
  /* v10.26: give every session its OWN id in the log.
   *
   * mypid is a global, set once in main() to FindTask(NULL). Upstream binkd
   * is fine with that: it is one process, and PID()/gettid() naturally differs
   * per thread. This port is not -- branch.c spawns each session as a separate
   * AmigaOS Process that SHARES one address space, so every session read the
   * same global and logged the same id:
   *
   *   - incoming from 192.168.86.1 (55650)   [1081768832]
   *   - incoming from 192.168.86.1 (55666)   [1081768832]
   *   - incoming from 192.168.86.1 (55668)   [1081768832]
   *
   * Three concurrent inbound sessions, indistinguishable, their handshakes
   * interleaved into what reads as triplicated lines. Evaluating the task
   * address at CALL time gives each Process its own value, which is what the
   * [id] field is for and what binkd's thread ids achieve elsewhere.
   *
   * amiga_task_id() lives in amiga_glue.c so this header need not pull in
   * proto/exec.h -- sys.h is included almost everywhere. */
  int amiga_task_id (void);
  #define PID() amiga_task_id()
#else
  extern int mypid;
  #define PID() mypid
#endif

#if defined(HAVE_FORK) && defined(HAVE_SIGPROCMASK) && defined(HAVE_WAITPID) && defined(SIG_BLOCK)
  void switchsignal(int how);
  #define blocksig()     switchsignal(SIG_BLOCK)
  #define unblocksig()   switchsignal(SIG_UNBLOCK)
  #define BLOCK_SIG      1
#else
  #define blocksig()
  #define unblocksig()
#endif

#ifndef F_OK
  #define F_OK 0
#endif

#ifndef HAVE_MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif

#ifndef EWOULDBLOCK
  #define EWOULDBLOCK EAGAIN
#endif

#ifndef O_BINARY
  #ifdef _O_BINARY
    #define O_BINARY _O_BINARY
  #else
    #define O_BINARY 0
  #endif
#endif

#ifndef O_NOINHERIT
  #ifdef _O_NOINHERIT
    #define O_NOINHERIT _O_NOINHERIT
  #else
    #define O_NOINHERIT 0
  #endif
#endif

#if defined(UNIX) || defined(AMIGA)
  /* To be sure rename will fail if the target exists */
  extern int o_rename (const char *from, const char *to);
  #define RENAME(f,t) o_rename(f,t)
#else
  #define RENAME(f,t) rename(f,t)
#endif

#ifdef AMIGA
  /* Sub-second pause between log-open retries -- see amiga/msleep.c. */
  extern void amiga_msleep (int ms);
  #define LOG_RETRY_DELAY() amiga_msleep(40)
#else
  #define LOG_RETRY_DELAY() do { } while (0)
#endif

#ifdef AMIGA
  /* The C runtime's unlink() can block forever on a file another task
   * holds, instead of returning an error -- it stalled an outbound poll
   * for 40+ minutes inside sdelete() on 2026-08-02, which in turn stalled
   * CNet's event scheduler. DeleteFile() is bounded and reports a proper
   * error, which is what sdelete()'s retry loop expects. See
   * amiga/delete.c. */
  extern int o_unlink (const char *path);
  /* v10.27: raw AmigaDOS error from the last o_unlink() failure. EACCES is
   * ambiguous -- ERROR_OBJECT_IN_USE (218) and ERROR_DELETE_PROTECTED (222)
   * both map to it, and they need opposite fixes. */
  extern long amiga_last_dos_err (void);
  #define UNLINK_DOSERR() amiga_last_dos_err()
  #define UNLINK(p) o_unlink(p)
#else
  #define UNLINK(p) unlink(p)
#endif

#ifdef AMIGA
  /* libnix keeps every open file on one global list and compiles its own
   * stdio locking out, but v10.5+ runs concurrent sessions as Processes
   * sharing a single address space -- so two of them opening or closing
   * files at once corrupt that list, and a recycled FILE hands one session
   * another's buffer. That is what put log text into a filename and, on
   * 2026-08-12, a log line into a DOS "insert volume" requester. Full
   * write-up in amiga/stdio.c.
   *
   * Redirected by macro rather than by an UNLINK()-style spelling on
   * purpose: a single unserialized open or close anywhere in the program
   * is enough to corrupt the list, so this must not rely on every call
   * site remembering to opt in. <stdio.h> is included above, so the real
   * declarations are already in scope. */
  extern void amiga_stdio_init (void);
  extern FILE *amiga_fopen (const char *path, const char *mode);
  extern FILE *amiga_fdopen (int fd, const char *mode);
  extern int   amiga_fclose (FILE *f);
  /* v10.24: libnix13's fstat() always reports st_size == 0 (it measures
   * with SEEK_CUR straight after seeking to the start).  That made binkd
   * treat every outbound file as empty, so every mail bundle this system
   * sent went out at 0 bytes.  See amiga/fstat.c for the disassembly and
   * the live evidence. */
  extern int amiga_fstat (int fd, struct stat *sb);
  #define fstat(d,b)  amiga_fstat(d,b)
  #define fopen(p,m)  amiga_fopen(p,m)
  #define fdopen(d,m) amiga_fdopen(d,m)
  #define fclose(f)   amiga_fclose(f)
#endif

#ifdef VISUALCPP
  #define sleep(a) Sleep((a)*1000)
  #define pipe(h)  _pipe(h, 0, 64)

  /* MSVC (Visual Studio) Versions (_MSC_VER):
   * 2010(10): 1600
   * 2005( 8): 1400
   * 2000( 6): 1200
   */

  /* [u]intmax_t introduced at least in MSVC 2010 */
  #if _MSC_VER >= 1600
    #include <stdint.h>
  #else
    typedef          __int64  intmax_t;
    typedef unsigned __int64 uintmax_t;
  #endif
  #define HAVE_INTMAX_T

  #if _MSC_VER <= 1200
    /* Only one type of 64-bit stat() in old versions */
    #define stat  _stati64
    #define fstat _fstati64
  #else
    /* There are FOUR types of stat() in these MSVC! (for all 32/64 bits time_t and filesize combinations) */
    /* Use "filesize: 64 bits, time_t: default" version */
    #define stat  _stat64
    #define fstat _fstat64
  #endif

  #if _MSC_VER <= 1200
    /* These functions exists, but in static libraries only and not declared in headers... */
    #ifdef _DLL  /* /MD option - with runtime DLL */
      #error Use static build to get 64-bit file I/O (nmake ... STATIC=1)
    #endif
    _CRTIMP int     __cdecl _fseeki64(FILE *, __int64, int);
    _CRTIMP __int64 __cdecl _ftelli64(FILE *);
  #endif
  #define fseeko _fseeki64
  #define ftello _ftelli64
  #define HAVE_FSEEKO

  #define PRIdMAX "I64i"
  #define PRIuMAX "I64u"

  #if _MSC_VER <= 1200
    /* TODO: although nobody uses 2nd and 3rd parameters now, write normal wrapper - safe_umax(s) or whatever */
    #define strtoumax(s, p, n) _atoi64(s)
  #else
    #define strtoumax _strtoui64
  #endif
  #define HAVE_STRTOUMAX
#endif

#ifdef __MINGW32__
  #define sleep(a) Sleep((a)*1000)
  #define pipe(h)  _pipe(h, 16384, _O_BINARY)
#endif

#if defined(WIN32)
  /* if no waiting sockets, call Sleep() instead of select() */
  #define SELECT(n, r, w, e, t)   \
	(((r)==0 || !FD_ISSET(n-1,r)) && ((w)==0 || !FD_ISSET(n-1,w)) ? \
	 Sleep((t)->tv_sec*1000+((t)->tv_usec+999)/1000),0 : select(n,r,w,e,t))
#else
  #define SELECT(n, r, w, e, t)  select(n, r, w, e, t)
#endif

#if defined(EBADTYPE) && !defined(ENOTDIR)
  #define ENOTDIR EBADTYPE /* __IBMC__ */
#endif

#ifndef HAVE_SNPRINTF
#ifdef HAVE_STDARG_H
int snprintf (char *str, size_t count, const char *fmt,...);
#else
int snprintf (va_alist) va_dcl;
#endif
#endif
#ifndef HAVE_VSNPRINTF
int vsnprintf (char *str, size_t count, const char *fmt, va_list args);
#endif

#if defined(__WATCOMC__) || defined(VISUALCPP) || defined(__MINGW32__) || defined(IBMC) || defined(__MSC__)
  #define MKDIR(s) mkdir(s)
#else
  #define MKDIR(s) mkdir(s, 0755)
#endif

#if defined(__WATCOMC__) || defined(__EMX__) /* expand list if necessary */
  #define BEGINTHREAD(a, b, c)   _beginthread(a, NULL, b, c)
  #define ENDTHREAD()            _endthread()
#elif defined(WITH_PTHREADS)
  /* #define BEGINTHREAD(a, b, c)   pthread_create(NULL, NULL, a, c) */
  #define ENDTHREAD()            pthread_exit(NULL)
#elif defined(HAVE_THREADS)
  #define BEGINTHREAD(a, b, c)   _beginthread(a, b, c)
  #define ENDTHREAD()            _endthread()
#endif

/* 
 * We cannot use off_t because Microsoft, as usual, sucks - in MSVC off_t
 * is forced to 32 bits. So, DON'T USE OFF_T ANYWHERE IN THE CODE.
 * Use our own boff_t instead.
 */
#ifdef VISUALCPP
  typedef __int64 boff_t;
#else
  typedef off_t   boff_t;
#endif

typedef unsigned char u8;
#if defined(SIZEOF_INT) && SIZEOF_INT!=0
#if SIZEOF_SHORT==2
typedef unsigned short int u16;
#else
#error Cannot find type for 16-bit integer!
#endif
#if SIZEOF_INT==4
typedef unsigned int u32;
#elif SIZEOF_LONG==4
typedef unsigned long int u32;
#else
#error Cannot find type for 32-bit integer!
#endif
#else /* SIZEOF undefined, use defaults */
typedef unsigned short int u16;
typedef unsigned long int u32;
#endif

#ifndef HAVE_INTMAX_T
 typedef long int intmax_t;
 #undef PRIdMAX
 #define PRIdMAX "ld"
 typedef unsigned long int uintmax_t;
 #undef PRIuMAX
 #define PRIuMAX "lu"
#endif

#ifdef AMIGA
/* This toolchain's intmax_t/uintmax_t are "long long" (8 bytes), even
 * though this is a 32-bit target - confirmed via __UINTMAX_TYPE__. The
 * generic "ld"/"lu" fallback below assumes a 4-byte long, which silently
 * truncates/misaligns every varargs slot after a PRIuMAX/PRIdMAX-formatted
 * argument on this big-endian CPU (each 8-byte value only half-consumed,
 * shifting all subsequent arguments in the same printf/sprintf call by 4
 * bytes) - this is what was producing garbled numbers like "(0 byte(s),
 * off 7475)" instead of "(7475 byte(s), off 0)" in the logs, and corrupted
 * the same fields in our own outgoing M_GET/M_FILE protocol messages. */
 #undef PRIdMAX
 #define PRIdMAX "lld"
 #undef PRIuMAX
 #define PRIuMAX "llu"
#endif

#ifndef PRIdMAX
#define PRIdMAX "ld"
#define PRIuMAX "lu"
#endif

#ifndef HAVE_STRTOUMAX
#define strtoumax(ptr, endptr, base)	strtoul(ptr, endptr, base)
#endif

#ifndef HAVE_FSEEKO
#define fseeko(f, offset, whence)	fseek(f, (long)(offset), whence)
#define ftello(f)			(boff_t)ftell(f)
#endif

#define UNUSED_ARG(s)  (void)(s)

#if defined(HAVE_GETTIMEOFDAY)
#define gettvtime(tv)	gettimeofday(tv, NULL)
#elif defined(WIN32)
void gettvtime(struct timeval *tv);
#else
#define gettvtime(tv)	((tv)->tv_sec=time(NULL),(tv)->tv_usec=0)
#endif

#endif
