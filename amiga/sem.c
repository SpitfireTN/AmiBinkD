/*
 *  Amiga semaphores
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
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

/* Non-blocking acquire. AttemptSemaphore() returns nonzero if it got the
 * semaphore and zero if another Task holds it; it never waits, so a caller
 * that can afford to skip its work (bsy_touch()) cannot be stranded behind
 * whoever currently holds the lock. See bsy_touch() for why that matters. */
int _TryLockSem(void *vpSem) {
  return AttemptSemaphore ((struct SignalSemaphore *)vpSem) ? 1 : 0;
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

    /* SetSignal(0, mask) atomically clears the bits and returns what they
     * were beforehand - used to check (not set) CTRL_C. Avoids
     * CheckSignal(): this toolchain's <proto/dos.h> resolves to an older
     * ndk13-include variant that doesn't declare it at all. */
    if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
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

/* ------------------------------------------------------------------ *
 * v10.25: a PUBLIC semaphore for the log.
 *
 * _InitSem() above builds a private SignalSemaphore in the caller's own
 * memory. That serialises writers inside ONE AmiBinkd instance, which is all
 * upstream binkd ever needs -- it is a single process with threads.
 *
 * This port is not. The inbound server and each 30-minute poll are SEPARATE
 * program invocations, so there are two or three AmiBinkd instances alive at
 * once, each with its own private lsem, all fprintf()ing the same log file.
 * Nothing serialises them against each other, and their output interleaves
 * mid-line. Observed 2026-08-18:
 *
 *     + 18 Aug 21:07:52 [1081963032] pwd protected session (MD5)
 *     nux/64                      <- torn out of "Linux/64"
 *     115                         <- torn out of "binkd/1.1a-115"
 *       18 Aug 19:41:42 [1081963032] servmgr started
 *
 * Two lines shredded into fragments, and a 19:41 line landing after a 21:07
 * one. Exec's public semaphore list is the standard AmigaOS answer: every
 * instance looks up the SAME named semaphore, so the log lock finally spans
 * processes.
 *
 * Forbid()/Permit() around the find-or-create is required -- without it two
 * instances starting simultaneously can both fail the lookup and both add a
 * semaphore under the same name.
 * ------------------------------------------------------------------ */

#define AMIGA_LOG_SEM_NAME "AmiBinkd.log"

void *amiga_public_log_sem(void)
{
    static struct SignalSemaphore *cached = NULL;
    struct SignalSemaphore *sem;

    if (cached != NULL)
        return cached;

    Forbid();
    sem = (struct SignalSemaphore *) FindSemaphore ((STRPTR) AMIGA_LOG_SEM_NAME);
    if (sem == NULL)
    {
        sem = (struct SignalSemaphore *)
              AllocMem (sizeof (struct SignalSemaphore), MEMF_PUBLIC | MEMF_CLEAR);
        if (sem != NULL)
        {
            InitSemaphore (sem);
            sem->ss_Link.ln_Name = (char *) AMIGA_LOG_SEM_NAME;
            sem->ss_Link.ln_Pri  = 0;
            AddSemaphore (sem);
        }
    }
    Permit();

    cached = sem;
    return sem;
}
