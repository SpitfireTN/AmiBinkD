/*
 *  Amiga semaphores
 */
#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "../sem.h"

extern void Log (int lev, char *s,...);

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

/* Returns 0 if posted (and consumes the post), -1 on timeout.
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

    if (sec <= 0 || elapsed >= sec)
      return -1;

    Delay(AMIGA_DELAY_TICKS_PER_SEC);
    elapsed++;
  }
}
