/*
 *  branch.c -- Create co-processes
 *
 *  branch.c is a part of binkd project
 *
 *  Copyright (C) 1996-1998  Dima Maloff, 5047/13
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version. See COPYING.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sys.h"
#include "common.h"
#include "tools.h"
#include "sem.h"

#ifdef AMIGA
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* This toolchain's ndk13-include search path (checked before the real
 * ndk-include, same gotcha already documented in amiga_glue.c for
 * proto/dos.h's CheckSignal) shadows <utility/tagitem.h> with a literal
 * empty 1-byte stub file, and its clib/dos_protos.h doesn't declare
 * CreateNewProcTags() at all - so TAG_USER/TAG_DONE and the prototype
 * below never actually arrive via the #includes above. Declared here
 * directly rather than fighting global include search order (which
 * would risk changing what every other file in this codebase sees). */
#ifndef TAG_DONE
#define TAG_DONE ((ULONG) 0)
#endif
#ifndef TAG_USER
#define TAG_USER ((ULONG) (1UL << 31))
#endif
struct Process * __stdargs CreateNewProcTags (ULONG tag1type, ...);
#endif

/* AmigaOS has no fork()/vfork() without ixemul.library, which this port
 * deliberately avoids. Through v10.4, AMIGA ran every session
 * synchronously (see the old comment this replaced) - no concurrency at
 * all. v10.5 adds real concurrency via CreateNewProcTags(NP_Entry=...),
 * since classic AmigaOS has one flat shared address space (no fork()-
 * style address-space isolation) - a process spawned this way sees the
 * same globals/heap as its parent, closer to a thread than to fork().
 *
 * NP_Entry takes a function pointer with no parameters - there's no
 * built-in way to hand it a startup argument. The obvious "shared global
 * + signal/semaphore handshake" approach has a real pitfall: this
 * codebase's own SIGF_SINGLE is the same physical bit as SIGF_BLIT
 * (graphics.library's blitter arbitration signal, exec/tasks.h) - not a
 * documented, safe idiom for this. Used instead: give the child NP_
 * Priority one below the parent's current priority. AmigaOS is strictly
 * priority-preemptive (a ready lower-priority task never runs while a
 * ready higher-priority task hasn't blocked), and the parent makes no
 * blocking/DOS call between CreateNewProcTags() returning and storing
 * the args pointer in the child's own tc_UserData - so the child cannot
 * observe tc_UserData before the parent has set it, without needing any
 * extra synchronization primitive. */
#ifdef AMIGA
typedef struct {
  void (*F) (void *);
  void *arg;
} amiga_proc_args_t;

static void amiga_proc_trampoline (void)
{
  struct Task *me = FindTask (NULL);
  amiga_proc_args_t *pa = (amiga_proc_args_t *) me->tc_UserData;
  void (*F) (void *) = pa->F;
  void *arg = pa->arg;

  free (pa);
  /* v10.12: tc_UserData must go back to a known-safe NULL here, not stay
   * a dangling pointer to what was just freed - amiga_glue.c's new
   * amiga_current_socketbase() trusts a non-NULL tc_UserData to be a
   * genuine private bsdsocket.library base installed by
   * amiga_open_private_socketbase(), for every child spawned via this
   * trampoline (not just client.c's outbound ones). Without this reset,
   * every freshly spawned child - including inbound session children
   * that must keep using the shared SocketBase exactly as before -
   * would read garbage through that lookup. */
  me->tc_UserData = NULL;
  F (arg);
}
#endif

#ifdef WITH_PTHREADS
typedef struct {
    void (*F) (void *);
    void *args;
    MUTEXSEM mutex;
#ifdef HAVE_GETTID
    pid_t tid;
#endif
  } thread_args_t;

static void *thread_start(void *arg)
{
  void (*F) (void*);
  void *args;

  F = ((thread_args_t *)arg)->F;
  args = ((thread_args_t *)arg)->args;
#ifdef HAVE_GETTID
  ((thread_args_t *)arg)->tid = PID();
#endif
  ReleaseSem(&((thread_args_t *)arg)->mutex);
  pthread_detach(pthread_self());
  F(args);
  return NULL;
}
#endif

int branch (register void (*F) (void *), register void *arg, register size_t size)
{
  register int rc;
  char *tmp;

  /* We make our own copy of arg for the child as the parent may destroy it
   * before the child finish to use it. It's not really needed with fork()
   * but we do not want extra checks for HAVE_FORK before free(arg) in the
   * child. */
  if (size > 0)
  {
    if ((tmp = malloc (size)) == NULL)
    {
      Log (1, "malloc failed");
      return -1;
    }
    else
    {
      memcpy (tmp, arg, size);
      arg = tmp;
    }
  }
  else
    arg = 0;

#if defined(HAVE_FORK) && !defined(HAVE_THREADS) && !defined(AMIGA) && !defined(DEBUGCHILD)
again:
  if (!(rc = fork ()))
  {
    /* new process */
    mypid = getpid();
    F (arg);
    exit (0);
  }
  else if (rc < 0)
  {
    if (errno == EINTR) goto again;
    /* parent, error */
    Log (1, "fork: %s", strerror (errno));
  }
  else
  {
    /* parent, free our copy of args */
    xfree (arg);
  }
#endif

#if defined(HAVE_THREADS) && !defined(DEBUGCHILD)
  #ifdef WITH_PTHREADS
  { thread_args_t args;
    pthread_t tid;

    args.F = F;
    args.args = arg;
    InitSem(&args.mutex);
    LockSem(&args.mutex);
    if ((rc = pthread_create (&tid, NULL, thread_start, &args)) != 0)
    {
      Log (1, "pthread_create: %s", strerror (rc));
      rc = -1;
    }
    else
    {
      LockSem(&args.mutex); /* wait until thread releases this mutex */
      #ifdef HAVE_GETTID
      rc = args.tid;
      #else
      rc = (int)(0xffff & (long int)tid);
      #endif
    }
    ReleaseSem(&args.mutex);
    CleanSem(&args.mutex);
  }
  #else
  if ((rc = BEGINTHREAD (F, STACKSIZE, arg)) < 0)
    Log (1, "_beginthread: %s", strerror (errno));
  #endif
#endif

#if defined(AMIGA) && !defined(DEBUGCHILD)
  {
    amiga_proc_args_t *pa;
    struct Process *newproc;
    struct Task *me = FindTask (NULL);

    if ((pa = malloc (sizeof (*pa))) == NULL)
    {
      Log (1, "malloc failed");
      xfree (arg);
      rc = -1;
    }
    else
    {
      pa->F = F;
      pa->arg = arg;

      newproc = CreateNewProcTags (
          NP_Entry, (ULONG) amiga_proc_trampoline,
          NP_StackSize, (ULONG) STACKSIZE,
          NP_Priority, (LONG) (me->tc_Node.ln_Pri - 1),
          NP_Name, (ULONG) "AmiBinkD session",
          /* Session children do the actual file I/O, so they are the ones
           * most likely to hand DOS a bad path. -1 = never put up a
           * requester, return the error to the caller instead. Set here
           * explicitly rather than relying on inheriting the parent's
           * pr_WindowPtr (binkd.c main); a child that blocks on a
           * requester strands the whole poll. */
          NP_WindowPtr, (ULONG) -1,
          TAG_DONE);

      if (newproc == NULL)
      {
        Log (1, "CreateNewProcTags failed");
        free (pa);
        xfree (arg);
        rc = -1;
      }
      else
      {
        newproc->pr_Task.tc_UserData = pa;
        rc = 0;
      }
    }
  }
#endif

#if defined(DOS) || defined(DEBUGCHILD)
  /* No fork()/threads here. Run synchronously: one binkp session at a
   * time, no concurrency - same as AMIGA through v10.4 (see the comment
   * above the AMIGA case for why that changed; DEBUGCHILD always stays
   * synchronous regardless of platform, since it exists specifically to
   * make session handling easy to single-step in a debugger). */
  rc = 0;
  F (arg);
#endif

  return rc;
}
