/*
 *  exitproc.c -- Actions to perform on exit()
 *
 *  exitproc.c is a part of binkd project
 *
 *  Copyright (C) 1997  Dima Maloff, 5047/13
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version. See COPYING.
 */

#include <signal.h>

#include "sys.h"
#include "readcfg.h"
#include "common.h"
#include "ftnnode.h"
#include "bsy.h"
#include "tools.h"
#include "sem.h"
#include "server.h"
#ifdef WITH_PERL
#include "perlhooks.h"
#endif
#ifdef BINKD9X
#include "nt/win9x.h"
#endif
#if defined(WIN32) && !defined(BINKD9X)
#include "nt/service.h"
#include "nt/w32tools.h"
#endif

int binkd_exit;

#ifdef HAVE_THREADS

static fd_set sockets;
static SOCKET max_socket;

int add_socket(SOCKET sockfd)
{
  threadsafe(
    FD_SET (sockfd, &sockets);
    if (sockfd >= max_socket)
      max_socket = sockfd + 1;
  );
  return 0;
}

int del_socket(SOCKET sockfd)
{
  threadsafe(FD_CLR (sockfd, &sockets));
  return 0;
}

#endif

void close_srvmgr_socket(void)
{
  int curfd;

  for (curfd=0; curfd<sockfd_used; curfd++)
  { Log (5, "Closing server socket # %i", sockfd[curfd]);
    soclose (sockfd[curfd]);
  }
  sockfd_used = 0;
}

void exitfunc (void)
{
  BINKD_CONFIG *config;
#if defined(WIN32) && !defined(BINKD9X)
  static int exitfunc_called_flag=0;

  if (IsNT() && isService()) {
    LockSem(&exitsem);
    if(exitfunc_called_flag)
    { /* prevent double call exitfunc() at NT service stop sequence */
      ReleaseSem(&exitsem);
      Log(10, "exitfunc() repeated call, return from exitfunc()");
      return;
    }
    exitfunc_called_flag=1;
    ReleaseSem(&exitsem);
  }
#endif

  Log(7, "exitfunc()");

#if defined(HAVE_THREADS)
  /* exit all threads */
  { SOCKET h;
    int timeout = 0;
    /* wait for threads exit */
    binkd_exit = 1;
    for (;;)
      if (n_servers || n_clients || pidcmgr || pidsmgr)
      {
	close_srvmgr_socket();
	if (pidcmgr)
	  PostSem(&wakecmgr);
	/* close active sockets */
	for (h=0; h < max_socket; h++)
	  if (FD_ISSET(h, &sockets))
	    soclose (h);

	if (WaitSem (&eothread, 1))
	{
	  timeout++;
	  if (timeout == 4) /* 4 sec */
	  {
	    Log(5, "exitfunc(): warning, threads exit timeout (%i sec), n_servers %i, n_clients %i pidcmgr %i pidsmgr %i!",
			    timeout, n_servers, n_clients, (int)pidcmgr, (int)pidsmgr);
	    break;
	  }
	}
	else
	{
	  Log(9, "Thread finished");
	  timeout = 0;
	}
      }
      else
      {
	Log(8, "exitfunc(): all threads finished");
	break;
      }
  }
#elif defined(HAVE_FORK)
  if (pidcmgr)
  { int i;
    i=pidcmgr, pidcmgr=0; /* prevent abort when cmgr exits */
    kill (i, SIGTERM);
    /* sleep (1); */
  }
  close_srvmgr_socket();
#elif defined(AMIGA)
  /* v10.5: real concurrent sessions (branch.c, CreateNewProcTags) need a
   * drain here too - through v10.4 branch() always ran synchronously,
   * so exitfunc() could never be reached with a session still in
   * flight. This is deliberately simpler than the HAVE_THREADS case
   * above (no pidcmgr/pidsmgr tracking or add_socket()/del_socket()
   * force-close of in-flight sockets - neither is wired up for AMIGA),
   * and deliberately does NOT give up after a few seconds and fall
   * through the way the HAVE_THREADS case does: CreateNewProcTags's
   * NP_Entry child has no pr_SegList of its own (unlike NP_Seglist) -
   * only the parent's segment list references the code it's running.
   * If this process's own exit() actually completed while a spawned
   * session process was still running, that child could end up
   * executing out of memory this process's exit teardown just freed -
   * a real crash risk, not just a cosmetic timeout warning like the
   * HAVE_THREADS path's. Waiting indefinitely (with periodic logging,
   * not a silent hang) is the safer failure mode here: a wedged session
   * blocks shutdown rather than risking a crash, and it's recoverable
   * the same way this BBS's own stuck-process incidents already are
   * (kill and relaunch the whole Amiberry instance from the host side)
   * if it ever actually happens. */
  { int waited = 0;

    binkd_exit = 1;
    while (n_servers || n_clients)
    {
      if (WaitSem (&eothread, 1))
      {
        waited++;
        if (waited % 10 == 0)
          Log (2, "exitfunc(): still waiting for %i session(s) to finish (%i sec) - not giving up, see v10.5 notes",
               n_servers + n_clients, waited);
      }
      else
      {
        Log (8, "exitfunc(): a session finished");
        waited = 0;
      }
    }
  }
#endif

  config = lock_current_config();
  if (config)
    bsy_remove_all (config);
  sock_deinit ();
  nodes_deinit ();
  if (config)
  {
    if (*config->pid_file && pidsmgr == (int) getpid ())
      delete (config->pid_file);
    /* completely unload config */
#if defined(HAVE_FORK) && !defined(HAVE_THREADS)
    unlock_config_structure(config, inetd_flag || (!pidsmgr && pidCmgr == (int) getpid()) || (pidsmgr == (int) getpid()));
#else
    unlock_config_structure(config, 1);
#endif
  }
  CleanSem (&config_sem);
  CleanSem (&hostsem);
  CleanSem (&resolvsem);
  CleanSem (&lsem);
  CleanSem (&blsem);
  CleanSem (&varsem);
  CleanSem (&peernamesem);
  CleanEventSem (&eothread);
  CleanEventSem (&wakecmgr);
#ifdef OS2
  CleanSem (&fhsem);
#endif
#if defined(WITH_PERL) && defined(HAVE_THREADS) && defined(PERL_MULTITHREAD)
  CleanSem (&perlsem);
#endif
  ReleaseErrorList();
}
