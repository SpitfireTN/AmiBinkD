/* amiga/fstat.c -- working fstat() for AmiBinkD on m68k-amigaos/libnix13.
 *
 * WHY THIS EXISTS
 *
 * libnix13's own fstat() always reports st_size == 0.  Disassembling
 * fstat13.o shows it measure the file like this:
 *
 *     lseek (fd, 0, 0);            SEEK_SET  -- seek to the start
 *     st_mode = 0xA1FF;
 *     st_size = lseek (fd, 0, 1);  SEEK_CUR  -- we are AT 0, so this is 0
 *     lseek (fd, saved, -1);       -1 is not a valid POSIX whence either
 *
 * It should seek SEEK_END (whence 2) to measure the file.  libnix's lseek()
 * maps POSIX whence correctly (0 -> OFFSET_BEGINNING, 1 -> OFFSET_CURRENT,
 * 2 -> OFFSET_END), so asking for SEEK_CUR right after seeking to the start
 * can only ever return zero.  fstat() therefore SUCCEEDS and reports every
 * file as empty.
 *
 * WHAT IT BROKE
 *
 * protocol.c's start_file_transfer() does:
 *
 *     f = fopen (path, "rb");
 *     fstat (fileno (f), &sb);
 *     ... state->out.size = sb.st_size ...
 *
 * so binkd believed every outbound file was 0 bytes.  Live effect: every
 * mail bundle this system ever sent went out empty -- logged as
 * "sending Outbound:80.774.0.0.TU0 as 89d32be1.TU0 (0)" and
 * "QSIZE 0 files 0 bytes" -- while the bundle on disk was a perfectly good
 * ZIP containing the packet.  Confirmed 2026-08-18: a 456-byte valid
 * archive, closed, held open by nobody, sent as 0 bytes.
 *
 * Nothing logged an error at any layer, because no layer had one: CNet
 * packed correctly, binkd sent exactly what it was told the size was.
 *
 * THE FIX
 *
 * Measure with SEEK_END and restore the caller's position.  Everything else
 * is delegated to the real fstat so st_mode/st_blksize etc keep whatever
 * libnix already provides.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* The real one, before sys.h's macro redirects the name. */
extern int fstat (int fd, struct stat *sb);

int amiga_fstat (int fd, struct stat *sb)
{
  off_t cur, end;
  int rc;

  rc = fstat (fd, sb);
  if (rc != 0 || sb == NULL)
    return rc;

  /* Where is the caller's file position now? */
  if ((cur = lseek (fd, 0, SEEK_CUR)) == (off_t) -1)
    return rc;                    /* not seekable -- leave libnix's value */

  if ((end = lseek (fd, 0, SEEK_END)) == (off_t) -1)
    return rc;

  /* Put it back exactly where it was before we measured. */
  lseek (fd, cur, SEEK_SET);

  sb->st_size = end;
  return 0;
}
