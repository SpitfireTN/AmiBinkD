/*
 *  Native AmigaOS touch() -- set a file's datestamp.
 *
 *  Replaces the generic utime() in tools.c, which BLOCKS INDEFINITELY on
 *  this port. Caught live 2026-08-02 with step markers through inb_done():
 *  two independent sessions stalled after "touch in" and never reached
 *  "touch out", with the received .dt already complete on disk, the socket
 *  fully drained, and no Amiberry coredump (so not the emulator). Sixteen
 *  other commits went through the same path in the same window, which is
 *  why it shows up as roughly one session in ten rather than always.
 *
 *  The stalled session keeps its .bsy locks forever, so the leak that had
 *  been attributed in turn to the socket handoff, ND mode, temp-file
 *  collisions and log contention was, in the end, a file-date call --
 *  cosmetic metadata that binkd sets purely so the received file carries
 *  the sender's timestamp.
 *
 *  The file is already closed by this point (protocol.c fclose()s it
 *  before calling inb_done), so nothing here is contending with our own
 *  handle. SetFileDate() is a single, bounded dos.library call and is the
 *  same approach the port already takes for rename() and getfree().
 */
#include <dos/dos.h>
#include <proto/dos.h>
#include <errno.h>
#include <time.h>

/* AmigaOS counts from 1978-01-01, Unix from 1970-01-01: 2922 days. */
#define AMIGA_EPOCH_OFFSET  ((time_t) 252460800)
#define SECS_PER_DAY        86400
#define TICKS_PER_SEC       50            /* dos.library DateStamp ticks */

static int amiga_dos_err_to_errno (LONG dosErr)
{
  switch (dosErr)
  {
    case ERROR_OBJECT_NOT_FOUND:  return ENOENT;
    case ERROR_DIR_NOT_FOUND:     return ENOENT;
    case ERROR_OBJECT_IN_USE:     return EBUSY;
    case ERROR_WRITE_PROTECTED:   return EROFS;
    case ERROR_DISK_WRITE_PROTECTED: return EROFS;
    case ERROR_OBJECT_WRONG_TYPE: return EINVAL;
    default:                      return EIO;
  }
}

int touch (char *file, time_t t)
{
  struct DateStamp ds;
  time_t rel;

  if (file == NULL || *file == '\0')
  {
    errno = EINVAL;
    return -1;
  }

  /* A timestamp the Amiga epoch cannot represent is not worth failing the
   * whole transfer over -- the datestamp is cosmetic. Leave the file's own
   * date alone and report success. */
  if (t < AMIGA_EPOCH_OFFSET)
    return 0;

  rel = t - AMIGA_EPOCH_OFFSET;
  ds.ds_Days   = (LONG) (rel / SECS_PER_DAY);
  ds.ds_Minute = (LONG) ((rel % SECS_PER_DAY) / 60);
  ds.ds_Tick   = (LONG) ((rel % 60) * TICKS_PER_SEC);

  if (SetFileDate ((STRPTR) file, &ds))
    return 0;                              /* AmigaOS: nonzero == success */

  errno = amiga_dos_err_to_errno (IoErr ());
  return -1;
}
