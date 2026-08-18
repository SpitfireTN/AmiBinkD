/*
 *  amiga/stdio.c -- serialize the C runtime's global open-file list.
 *
 *  THE BUG THIS FIXES (diagnosed 2026-08-12, first seen 2026-07-30):
 *
 *  A filename buffer would come back holding a line of AmiBinkd's own log
 *  text. Twice, four versions apart:
 *
 *    v10.14  start_file_transfer:   30 Jul 05:32:51 [1094578264] BEGIN, ...
 *    v10.18  a dos.library path call, which made AmigaOS put up
 *            "Please insert volume ? 12 Aug 17 in any drive"
 *
 *  ("? 12 Aug 17" is not a volume. It is the log line
 *   "? 12 Aug 17:30:41 [1092491064] error unlinking ..." -- AmigaDOS takes
 *   everything before the first colon as a device name.)
 *
 *  Cause: since v10.5 each binkp session is a separate AmigaOS Process
 *  created with CreateNewProcTags(), and those share one flat address space
 *  -- see branch.c: a child "sees the same globals/heap as its parent,
 *  closer to a thread than to fork()". libnix keeps its open files on a
 *  single global list (symbol ____filelist, libnix13.a:__initstdio.o) which
 *  fopen()/fdopen()/fclose() mutate, and libnix's own stdio locking is
 *  compiled out: <stdio.h> defines __STDIO_LOCK()/__STDIO_UNLOCK() to
 *  nothing unless __posix_threads__ is defined, and -mcrt=nix13 does not
 *  define it.
 *
 *  So concurrent sessions plus the logger mutate an unlocked linked list.
 *  A FILE and its buffer freed by one Process get handed straight back out
 *  while another Process still holds the pointer, and a later fgets()
 *  returns whatever now occupies that memory. It is almost always log text,
 *  because vLog() does fopen+fprintf+fclose for every single line -- by far
 *  the heaviest file churn in the program. tools.c's lsem does not help:
 *  it only serializes other *log* writers, never a session's file I/O.
 *  That log line then becomes a "flo line", which is a filename.
 *
 *  The asymmetry that proves it: libnix's malloc.o does carry a semaphore
 *  (___memsema guarding ___memorylist), so malloc/free are already safe
 *  across Processes. The file list has no equivalent. libnix protected the
 *  heap and left the file list open.
 *
 *  WHY UPSTREAM BINKD NEVER HITS THIS. Config.h:17 lists four concurrency
 *  models and each of the other three is safe for a different reason:
 *  HAVE_FORK (Unix) gives every child its own address space, so two
 *  sessions cannot share a file list at all; HAVE_THREADS (Win32/OS2)
 *  shares an address space but MSVCRT and EMX ship thread-safe stdio, which
 *  C99 requires; DOS has no concurrency. AMIGA is a fourth model that has
 *  Win32's sharing with none of the runtime guarantees, so it needs the
 *  lock written by hand. Upstream already applies exactly this idea to
 *  other non-reentrant libc calls -- threadsafe() in sem.h wraps
 *  localtime()/gmtime() under HAVE_THREADS (tools.c:184, 267). It simply
 *  never had to extend it to stdio.
 *
 *  Only fopen(), fdopen() and fclose() touch the list, so only they are
 *  wrapped; per-stream reads and writes stay on their own FILE and need no
 *  lock. sys.h macro-redirects the three names, which is deliberate -- any
 *  unserialized open or close anywhere in the program is enough to corrupt
 *  the list, so this must not depend on remembering to use a wrapper.
 */
#include <exec/types.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <stdio.h>

/* This file provides the wrappers, so it needs the real functions. sys.h
 * is deliberately not included here; these guard against it arriving
 * indirectly some day. */
#undef fopen
#undef fdopen
#undef fclose

static struct SignalSemaphore stdio_sem;
static int stdio_sem_ready = 0;

/*
 * Called from main() before anything can open a file and before branch.c
 * can spawn the first session Process, so no lock is needed to set this up.
 * The _ready flag keeps a would-be early caller from touching an
 * uninitialized semaphore rather than guarding against concurrency.
 */
void amiga_stdio_init (void)
{
  if (!stdio_sem_ready)
  {
    InitSemaphore (&stdio_sem);
    stdio_sem_ready = 1;
  }
}

FILE *amiga_fopen (const char *path, const char *mode)
{
  FILE *f;

  if (!stdio_sem_ready)
    return fopen (path, mode);

  ObtainSemaphore (&stdio_sem);
  f = fopen (path, mode);
  ReleaseSemaphore (&stdio_sem);
  return f;
}

FILE *amiga_fdopen (int fd, const char *mode)
{
  FILE *f;

  if (!stdio_sem_ready)
    return fdopen (fd, mode);

  ObtainSemaphore (&stdio_sem);
  f = fdopen (fd, mode);
  ReleaseSemaphore (&stdio_sem);
  return f;
}

int amiga_fclose (FILE *f)
{
  int rc;

  if (!stdio_sem_ready)
    return fclose (f);

  ObtainSemaphore (&stdio_sem);
  rc = fclose (f);
  ReleaseSemaphore (&stdio_sem);
  return rc;
}
