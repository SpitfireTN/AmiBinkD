/*
 *  Amiga semaphores
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "../sem.h"

extern void Log (int lev, char *s,...);
/* Declared directly rather than pulling in common.h here (this file
 * doesn't follow the usual sys.h-must-come-first include order the rest
 * of the codebase needs) - it's the same "int binkd_exit;" defined in
 * exitproc.c and checked throughout client.c/server.c/protocol.c/etc. */
extern int binkd_exit;

/* ~1 second of Delay() ticks (50Hz PAL/NTSC vblank rate - close enough
 * for a mailer's polling granularity). */
#define AMIGA_DELAY_TICKS_PER_SEC 50


int _InitSem(void *vpSem) {
   memset(vpSem, 0, sizeof (struct SignalSemaphore));
   InitSemaphore ((struct SignalSemaphore*)vpSem);
   return(0);
}

int _CleanSem(void *vpSem) {
  return (0);
}

int _LockSem(void *vpSem) {
  ObtainSemaphore ((struct SignalSemaphore *)vpSem);
  return (0);
}

int _ReleaseSem(void *vpSem) {
  ReleaseSemaphore ((struct SignalSemaphore *)vpSem);
  return (0);
}

/*
 * "Event" semaphores: no ixemul, so no native Exec signal-wait wiring
 * here - just a mutex-protected flag, checked via a Delay()-based poll
 * loop. Fine for a synchronous, single-task binkd (see branch.c).
 */

int _InitEventSem(void *vpSem) {
  EVENTSEM *e = (EVENTSEM *)vpSem;
  memset(&e->sem, 0, sizeof(e->sem));
  InitSemaphore(&e->sem);
  e->posted = 0;
  return 0;
}

int _CleanEventSem(void *vpSem) {
  return 0;
}

int _PostSem(void *vpSem) {
  EVENTSEM *e = (EVENTSEM *)vpSem;
  ObtainSemaphore(&e->sem);
  e->posted = 1;
  ReleaseSemaphore(&e->sem);
  return 0;
}

/* Returns 0 if posted (and consumes the post), -1 on timeout or on
 * CTRL_C (binkd_exit is set in the latter case - same contract as
 * amiga_glue.c's select() wrapper, so callers using SLEEP()/WaitSem()
 * for a poll/rescan delay notice a break the same way they'd notice one
 * during a blocking select(), instead of only being interruptible while
 * actually waiting on network I/O).
 * sec <= 0 means "check once, don't block". */
int _WaitSem(void *vpSem, int sec) {
  EVENTSEM *e = (EVENTSEM *)vpSem;
  int elapsed = 0;

  for (;;) {
    ObtainSemaphore(&e->sem);
    if (e->posted) {
      e->posted = 0;
      ReleaseSemaphore(&e->sem);
      return 0;
    }
    ReleaseSemaphore(&e->sem);

    if (CheckSignal(SIGBREAKF_CTRL_C))
    {
      binkd_exit = 1;
      return -1;
    }

    if (sec <= 0 || elapsed >= sec)
      return -1;

    Delay(AMIGA_DELAY_TICKS_PER_SEC);
    elapsed++;
  }
}
