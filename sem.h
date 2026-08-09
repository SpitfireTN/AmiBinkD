/*
 *  sem.h -- semaphores for multithreaded version
 *
 *  sem.h is a part of binkd project
 *
 *  Copyright (C) 1996  Fydodor Ustinov, FIDONet 2:5020/79
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version. See COPYING.
 */

#ifndef _SEM_H_
#define _SEM_H_

#if defined(WIN32)

#include <windows.h>
typedef HANDLE MUTEXSEM;

typedef HANDLE EVENTSEM;

#elif defined(OS2)

#define INCL_DOS
#include <os2.h>
typedef HMTX MUTEXSEM;

typedef HEV  EVENTSEM;

#elif defined(AMIGA)

#include <exec/exec.h>
typedef struct SignalSemaphore MUTEXSEM;

/* No native AmigaOS "event" primitive is used here (no ixemul) - just a
 * mutex-protected posted flag, consumed via a short poll+Delay() loop in
 * amiga/sem.c's _WaitSem(). Good enough for a synchronous, single-task
 * binkd (see branch.c) where nothing waits on a truly concurrent poster. */
typedef struct {
  struct SignalSemaphore sem;
  volatile int posted;
} EVENTSEM;

#elif defined(WITH_PTHREADS)

#include <pthread.h>
typedef pthread_mutex_t MUTEXSEM;
typedef struct { pthread_cond_t cond;
                 pthread_mutex_t mutex;
               } EVENTSEM;

#endif


/*
 *    Initialise Semaphores.
 */

int _InitSem (void *);

/*
 *    Clean Semaphores.
 */

int _CleanSem (void *);

/*
 *    Wait & lock semaphore
 */

int _LockSem (void *);

/*
 *    Release Semaphore.
 */

int _ReleaseSem (void *);

/*
 *    Try to lock a semaphore without blocking.
 *    Returns nonzero if the lock was taken, 0 if it was already held.
 */

int _TryLockSem (void *);

/*
 *    Initialise Event Semaphores.
 */

int _InitEventSem (void *);

/*
 *    Post Semaphore.
 */

int _PostSem (void *);

/*
 *    Wait Semaphore.
 */

int _WaitSem (void *, int);

/*
 *    Clean Event Semaphores.
 */

int _CleanEventSem (void *);

#if defined(WITH_PTHREADS)
  #define InitSem(sem)       pthread_mutex_init(sem, NULL)
  #define CleanSem(sem)      pthread_mutex_destroy(sem)
  #define LockSem(sem)       pthread_mutex_lock(sem)
  #define TryLockSem(sem)    (pthread_mutex_trylock(sem) == 0)
  #define ReleaseSem(sem)    pthread_mutex_unlock(sem)
  #define InitEventSem(sem)  (pthread_cond_init(&((sem)->cond), NULL), pthread_mutex_init(&((sem)->mutex), NULL))
  #define PostSem(sem)       (LockSem(&((sem)->mutex)), pthread_cond_signal(&((sem)->cond)), ReleaseSem(&((sem)->mutex)))
  #define WaitSem(sem, sec)  _WaitSem(sem, sec)
  #define CleanEventSem(sem) (pthread_cond_destroy(&((sem)->cond)), pthread_mutex_destroy(&((sem)->mutex)))
#elif defined(HAVE_THREADS) || defined(AMIGA)
  #define InitSem(vpSem) _InitSem(vpSem)
  #define CleanSem(vpSem) _CleanSem(vpSem)
  #define LockSem(vpSem) _LockSem(vpSem)
  #if defined(AMIGA)
    #define TryLockSem(vpSem) _TryLockSem(vpSem)
  #else
  /* WIN32/OS2 have no _TryLockSem implementation here: keep their existing
   * blocking behaviour rather than pretend at a non-blocking acquire. Only
   * the AmigaOS port has the stranding problem this is here to solve. */
    #define TryLockSem(vpSem) (LockSem(vpSem), 1)
  #endif
  #define ReleaseSem(vpSem) _ReleaseSem(vpSem)
  #define InitEventSem(vpSem) _InitEventSem(vpSem)
  #define PostSem(vpSem) _PostSem(vpSem)
  #define WaitSem(vpSem, sec) _WaitSem(vpSem, sec)
  #define CleanEventSem(vpSem) _CleanEventSem(vpSem)
#else		/* Do nothing */
  #define InitSem(vpSem)
  #define CleanSem(vpSem)
  #define LockSem(vpSem)
  #define TryLockSem(vpSem) (1)
  #define ReleaseSem(vpSem)
  #define InitEventSem(vpSem)
  #define PostSem(vpSem)
  #define WaitSem(vpSem, sec)
  #define CleanEventSem(vpSem)
#endif

#if defined(HAVE_THREADS) || defined(AMIGA)
extern MUTEXSEM hostsem;
extern MUTEXSEM resolvsem;
extern MUTEXSEM lsem;
extern MUTEXSEM blsem;
extern MUTEXSEM varsem;
extern MUTEXSEM config_sem;
/* v10.7 (AMIGA only in practice): guards protocol()'s peer/own-socket
 * identity resolution (getpeername()/getsockname() and the TCPERR()/errno
 * logging right after each) - see protocol.c. Needed because this
 * toolchain's errno (libnix, TCPERR()/TCPERRNO -> strerror(errno)/errno)
 * is a single plain global, not per-Task/per-Process storage, and classic
 * AmigaOS's flat shared address space means every CreateNewProcTags
 * session Process reads/writes the exact same memory location for it -
 * a sibling session's own syscall can silently reset it between this
 * session's failing call and the Log() that reads it back. Deliberately
 * a dedicated semaphore, not a reuse of hostsem/resolvsem, since this
 * critical section itself calls into code that already takes hostsem/
 * resolvsem internally (rfc2553.c) - reusing either here would risk a
 * self-deadlock. */
extern MUTEXSEM peernamesem;
extern EVENTSEM eothread;
extern EVENTSEM wakecmgr;
#define lockhostsem()		LockSem(&hostsem)
#define releasehostsem()	ReleaseSem(&hostsem)
#define lockresolvsem()		LockSem(&resolvsem)
#define releaseresolvsem()	ReleaseSem(&resolvsem)
#define threadsafe(exp)		LockSem(&varsem); exp; ReleaseSem(&varsem)
#if defined(PERL_MULTITHREAD)
#define lockperlsem()		LockSem(&perlsem);
#define releaseperlsem()	ReleaseSem(&perlsem);
#else
#define lockperlsem()
#define releaseperlsem()
#endif
#ifdef OS2
extern MUTEXSEM fhsem;
#endif
#ifdef WIN32
extern MUTEXSEM iconsem;
#endif
#else
#define lockhostsem()
#define releasehostsem()
#define lockresolvsem()
#define releaseresolvsem()
#define lockperlsem()
#define releaseperlsem()
#define threadsafe(exp)		exp
#endif

#endif
