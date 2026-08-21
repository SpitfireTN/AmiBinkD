/* amiga/dosio.c -- append to a file WITHOUT going through libnix's file
 * descriptor layer.
 *
 * WHY THIS EXISTS (read before "simplifying" Log() back to fopen):
 *
 * libnix hands out every file descriptor from ___allocfd (open.o), which
 * manipulates two GLOBALS declared in __initstdio.o:
 *
 *     ___stdfiledes    the fd -> entry array
 *     ___stdfilesize   its length
 *
 * ___allocfd scans that array for a free slot, and grows it with realloc(),
 * reassigning the global pointer. Disassembled 2026-08-21: there is NO
 * Forbid()/Permit(), no semaphore, nothing atomic -- and the same holds for
 * every other object in that layer (open, close, write, read, lseek, fopen,
 * fwrite, __initstdio all contain ZERO locking calls).
 *
 * branch() runs each session as a separate Process sharing ONE address
 * space, so they all share that table. Two Processes opening files at the
 * same moment can both pick the same free slot; the loser's descriptor then
 * refers to the winner's file, and its next write lands in the wrong file.
 *
 * That is not a theory. On 2026-08-20 a .bsy lock file was found containing
 * a fragment of a log line, and a purpose-built test (tests/logtest3.c)
 * reproduced it on demand: 92 of 1000 log lines lost and 4 lock-file markers
 * written into the log. It needs TWO different files open at once, which is
 * why ~4,400 writes across four single-file tests never showed it.
 *
 * Log() is by far the heaviest user of that path -- one open and one close
 * per line. Routing it through dos.library directly takes it out of the
 * table entirely, so it can neither corrupt another file nor be corrupted
 * by one.
 *
 * This does NOT fix the library. Anything still using stdio or open() can
 * still collide with itself.
 */

#include <string.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

/* Append len bytes to path, creating it if absent.
 * Returns 0 on success, -1 on failure. Never touches ___stdfiledes. */
int amiga_dos_append (const char *path, const char *data, int len)
{
    BPTR fh;

    if (path == NULL || data == NULL || len <= 0)
        return -1;

    /* MODE_READWRITE opens an existing file or creates a new one, and does
     * NOT truncate -- unlike MODE_NEWFILE. */
    fh = Open ((STRPTR) path, MODE_READWRITE);
    if (fh == 0)
        return -1;

    /* Seek explicitly rather than trusting an append mode: the position
     * after Open is the start of the file. */
    if (Seek (fh, 0, OFFSET_END) == -1)
    {
        Close (fh);
        return -1;
    }

    if (Write (fh, (APTR) data, (LONG) len) != (LONG) len)
    {
        Close (fh);
        return -1;
    }

    Close (fh);
    return 0;
}
