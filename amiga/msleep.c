/*
 *  Sub-second sleep for AmigaOS.
 *
 *  Needed because tools.c's Log() retries opening the log file ten times
 *  in a tight loop with NO delay between attempts -- ten fopen() calls
 *  take microseconds, so a competing task holding the file for even a few
 *  milliseconds makes all ten fail and the message is silently discarded.
 *
 *  That is not merely cosmetic. It cost real diagnostic evidence on
 *  2026-08-02: a session logged "DIAG inb_done: touch in" and then
 *  "done (from ... OK)" in the same second with the intervening
 *  "touch out" line simply missing -- which looked exactly like a hang in
 *  touch() and sent the investigation after a bug that was not there.
 *  Sysops lose log lines the same way, silently, whenever sessions
 *  overlap.
 *
 *  sleep(1) is far too coarse to use between retries (ten seconds of
 *  stall per message). Delay() takes 50Hz ticks, so this gives the
 *  millisecond granularity the retry loop actually needs.
 */
#include <dos/dos.h>
#include <proto/dos.h>

#define TICKS_PER_SEC 50

void amiga_msleep (int ms)
{
  LONG ticks;

  if (ms <= 0)
    return;

  /* Round up: Delay(0) returns immediately and would restore the original
   * busy-loop behaviour for short waits. */
  ticks = (LONG) ((ms * TICKS_PER_SEC + 999) / 1000);
  if (ticks < 1)
    ticks = 1;

  Delay (ticks);
}
