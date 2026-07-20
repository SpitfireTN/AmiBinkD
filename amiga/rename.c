#include <dos/dos.h>
#include <proto/dos.h>
#include <errno.h>

/* Translate the AmigaDOS IoErr() code to a reasonable errno so the
 * "cannot rename ...: %s!" log in inbound.c is actually diagnostic
 * instead of always printing strerror(0) == "No error". */
static int amiga_dos_err_to_errno(LONG dosErr)
{
  switch (dosErr)
  {
    case ERROR_OBJECT_EXISTS:        return EEXIST;
    case ERROR_OBJECT_NOT_FOUND:     return ENOENT;
    case ERROR_DIR_NOT_FOUND:        return ENOENT;
    case ERROR_OBJECT_IN_USE:        return EBUSY;
    case ERROR_DISK_FULL:            return ENOSPC;
    case ERROR_DISK_WRITE_PROTECTED: return EROFS;
    case ERROR_WRITE_PROTECTED:      return EROFS;
    case ERROR_RENAME_ACROSS_DEVICES: return EXDEV;
    case ERROR_INVALID_COMPONENT_NAME: return EINVAL;
    case ERROR_NO_FREE_STORE:        return ENOMEM;
    default:                         return EIO;
  }
}

/* Rename() returns nonzero (AmigaDOS BOOL convention, like Lock()/
 * CreateDir()/DeleteFile()) on success - callers here (inbound.c) expect
 * POSIX rename() semantics (0 == success), so the sense has to be
 * inverted. The previous version of this function didn't invert it,
 * silently reporting every successful AmigaDOS rename as a failure (with
 * whatever stale errno happened to be lying around, since it never set
 * one either) - inbound.c's retry/rename-finalize loops would then bail
 * out on "failures" that had, in fact, already succeeded on disk. */
int o_rename(char *from, char *to)
{
  if (Rename((STRPTR)from, (STRPTR)to))	/* cross-volume move won't work */
  {
    return 0;
  }
  else
  {
    errno = amiga_dos_err_to_errno(IoErr());
    return -1;
  }
}
