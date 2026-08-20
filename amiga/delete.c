/*
 *  Native AmigaOS unlink() -- delete a file.
 *
 *  Replaces the generic unlink() used by tools.c's delete()/sdelete(),
 *  which BLOCKS INDEFINITELY on this port the same way utime() did (see
 *  amiga/touch.c). Caught live 2026-08-02 19:30: an outbound poll logged
 *
 *      found old .csy file for 39:902/0@amiganet
 *
 *  and then nothing at all for 40+ minutes. That message comes from
 *  ftnq.c immediately before sdelete(), and sdelete() is bounded -- five
 *  tries, one second apart, then it logs "error unlinking". Neither that
 *  error nor the "giving up" line ever appeared, so the task was stuck
 *  inside unlink() itself, not looping around it. No Amiberry coredump,
 *  so not the emulator.
 *
 *  The stalled poll never finished, which left CNet's event scheduler
 *  waiting on it -- the 20:00 cycle never started. Same shape as the
 *  touch() hang, different call.
 *
 *  DeleteFile() is a single bounded dos.library call and returns a proper
 *  error for a locked file instead of blocking, which is exactly what
 *  sdelete()'s retry loop was written to handle. Matches the approach
 *  already taken for rename(), getfree() and touch().
 */
#include <dos/dos.h>
#include <proto/dos.h>
#include <errno.h>

/* NOTE on the two EACCES cases below: ERROR_OBJECT_IN_USE and
 * ERROR_DELETE_PROTECTED both map to EACCES, so a caller cannot tell them
 * apart from errno alone.
 *
 * v10.27 tried to expose the raw IoErr() through a module static so bsy.c
 * could print it. That did not work and was removed in v10.29: binkd runs
 * each session as its own AmigaOS Process SHARING ONE ADDRESS SPACE, so a
 * file-scope static is written by every session at once. It reported 0 on all
 * 44 samples -- impossible, since IoErr()==0 maps to EIO while every message
 * said EACCES, and the two are assigned on consecutive lines below. Same
 * defect as the old global mypid (fixed in v10.26).
 *
 * If this distinction is ever needed again, return the code THROUGH THE CALL
 * -- an out-parameter or a distinct errno -- never through shared state. */
static int amiga_dos_err_to_errno (LONG dosErr)
{
  switch (dosErr)
  {
    case ERROR_OBJECT_NOT_FOUND:     return ENOENT;
    case ERROR_DIR_NOT_FOUND:        return ENOENT;
    /* A lock file still held open by a live session. sdelete() retries on
     * EACCES, which is the behaviour we want here. */
    case ERROR_OBJECT_IN_USE:        return EACCES;
    case ERROR_OBJECT_WRONG_TYPE:    return EISDIR;
    case ERROR_DELETE_PROTECTED:     return EACCES;
    case ERROR_WRITE_PROTECTED:      return EROFS;
    case ERROR_DISK_WRITE_PROTECTED: return EROFS;
    case ERROR_DIRECTORY_NOT_EMPTY:  return ENOTEMPTY;
    default:                         return EIO;
  }
}

int o_unlink (const char *path)
{
  if (path == NULL || *path == '\0')
  {
    errno = EINVAL;
    return -1;
  }

  if (DeleteFile ((STRPTR) path))
    return 0;                          /* AmigaOS: nonzero == success */

  errno = amiga_dos_err_to_errno (IoErr ());
  return -1;
}
