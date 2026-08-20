/*
 *  server.c -- Handles inbound connections
 *
 *  server.c is a part of binkd project
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
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#ifdef HAVE_FORK
#include <signal.h>
#include <sys/wait.h>
#endif

#include "sys.h"
#include "iphdr.h"
#include "readcfg.h"
#include "common.h"
#include "server.h"
#include "iptools.h"
#include "tools.h"
#include "protocol.h"
#include "assert.h"
#include "setpttl.h"
#include "sem.h"
#if defined(WITH_PERL)
#include "perlhooks.h"
#endif
#include "rfc2553.h"

int n_servers = 0;
int ext_rand = 0;

SOCKET sockfd[MAX_LISTENSOCK];
int sockfd_used = 0;

#ifdef AMIGA
/* What servmgr hands an inbound session child: a ReleaseSocket() transfer
 * id rather than a descriptor (descriptors are meaningless across
 * SocketBases), plus the address family needed to re-create it. See the
 * long comment at the ReleaseSocket() call in do_server(). */
typedef struct {
  int id;
  int family;
} serv_handoff_t;

/* bsdsocket.library's "allocate me an unused transfer id" sentinel for
 * ReleaseSocket(). Defined here for the same reason branch.c declares
 * CreateNewProcTags() itself: this toolchain's headers don't provide it.
 *
 * NOT USED for the handoff -- see next_handoff_id. Kept only to document
 * what the sentinel is, because reaching for it is the obvious mistake. */
#ifndef UNIQUE_ID
#define UNIQUE_ID (-1)
#endif

/* We allocate our own transfer ids rather than asking bsdsocket.library
 * for one with UNIQUE_ID.
 *
 * Amiberry's bsdsocket_emu returns the *same* id (65536) from every
 * ReleaseSocket(fd, UNIQUE_ID) call, so with more than one session being
 * handed off at once -- which is precisely what fixing inbound made
 * possible -- several sockets end up released under one id and the
 * children racing to ObtainSocket() it can pick up the wrong socket, or
 * one child can consume another's. Confirmed live: five concurrent
 * inbound sessions all logged `released ... as id 65536'.
 *
 * An explicit, monotonically increasing id per handoff removes the race
 * and costs nothing on a stack that does allocate unique ids properly. */
static LONG next_handoff_id = 0;
#endif

static void serv (void *arg)
{
#ifdef AMIGA
  int h;
  struct Library *privSocketBase;
#else
  int h = *(int *) arg;
#endif
  BINKD_CONFIG *config;
#if defined(WITH_PERL) && defined(HAVE_THREADS)
  void *cperl;
#endif

#if defined(HAVE_FORK) && !defined(HAVE_THREADS) && !defined(DEBUGCHILD)
  int curfd;
  pidcmgr = 0;
  for (curfd=0; curfd<sockfd_used; curfd++)
    soclose(sockfd[curfd]);
#endif

#ifdef AMIGA
  /* v10.16: claim our own bsdsocket.library instance, then re-materialise
   * the socket servmgr released to us inside it. Both halves matter: the
   * private base is what makes WaitSelect()'s readiness signals actually
   * arrive in *this* Process (see do_server()'s ReleaseSocket() comment),
   * and ObtainSocket() is what turns servmgr's transfer id back into a
   * descriptor that is valid here. */
  {
    serv_handoff_t ho = *(serv_handoff_t *) arg;
    LONG got;

    privSocketBase = amiga_open_private_socketbase ();
    if (privSocketBase == NULL)
      Log (1, "amiga_open_private_socketbase failed, falling back to shared bsdsocket.library");

    got = ObtainSocket ((LONG) ho.id, ho.family, SOCK_STREAM, 0);
    Log (7, "handoff: child base=%p obtained id %i -> fd %li",
         (void *) privSocketBase, ho.id, (long) got);
    if (got < 0)
    {
      Log (1, "serv ObtainSocket(): %s", TCPERR ());
      amiga_close_private_socketbase (privSocketBase);
      free (arg);
      rel_grow_handles (-6);
      threadsafe(--n_servers);
      PostSem(&eothread);
      return;
    }
    h = (int) got;
    add_socket (h);
  }
#endif

  config = lock_current_config();
#if defined(WITH_PERL) && defined(HAVE_THREADS)
  cperl = perl_init_clone(config);
#endif
  protocol (h, h, NULL, NULL, NULL, NULL, NULL, config);
  Log (5, "downing server...");
#if defined(WITH_PERL) && defined(HAVE_THREADS)
  perl_done_clone(cperl);
#endif
  del_socket(h);
  soclose (h);
#ifdef AMIGA
  /* After soclose(): the socket lives in this private base, so the base
   * has to outlive it. */
  Log (7, "handoff: child closing fd %i, base=%p, n_servers=%i",
       h, (void *) privSocketBase, n_servers);
  amiga_close_private_socketbase (privSocketBase);
#endif
  free (arg);
  unlock_config_structure(config, 0);
  rel_grow_handles (-6);
#ifdef HAVE_THREADS
  threadsafe(--n_servers);
  PostSem(&eothread);
  ENDTHREAD();
#elif defined(AMIGA)
  /* v10.5: see the matching comment in client.c's call() - was a bare
   * --n_servers, safe only under the old always-synchronous branch().
   * Real concurrent sessions need this threadsafe() now. */
  threadsafe(--n_servers);
  PostSem(&eothread);
#elif defined(DOS) || defined(DEBUGCHILD)
  --n_servers;
#endif
}

/*
 * Server manager.
 */

static int do_server(BINKD_CONFIG *config)
{
  struct addrinfo *ai, *aiHead, hints;
  int aiErr;
  SOCKET new_sockfd;
#ifdef AMIGA
  serv_handoff_t serv_handoff;
#endif
  int pid;
  socklen_t client_addr_len;
  struct sockaddr_storage client_addr;
  int opt = 1;
  int save_errno;
  struct listenchain *listen_list;

  /* setup hints for getaddrinfo */
  memset((void *)&hints, 0, sizeof(hints));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  for (listen_list = config->listen.first; listen_list; listen_list = listen_list->next)
  {
    if ((aiErr = getaddrinfo(listen_list->addr[0] ? listen_list->addr : NULL, 
                             listen_list->port, &hints, &aiHead)) != 0)
    {
      Log(1, "servmgr getaddrinfo: %s (%d)", gai_strerror(aiErr), aiErr);
      return -1;
    }

    for (ai = aiHead; ai != NULL && sockfd_used < MAX_LISTENSOCK; ai = ai->ai_next)
    {
      sockfd[sockfd_used] = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (sockfd[sockfd_used] < 0)
      {
        Log(1, "servmgr socket(): %s", TCPERR ());
        return -1;
      }
#ifdef UNIX /* Not sure how to set NOINHERIT flag for socket on Windows and OS/2 */
      if (fcntl(sockfd[sockfd_used], F_SETFD, FD_CLOEXEC) != 0)
        Log(1, "servmgr fcntl set FD_CLOEXEC error: %s", strerror(errno));
#endif
#ifdef IPV6_V6ONLY
      if (ai->ai_family == PF_INET6)
      {
        int v6only = 1;
        if (setsockopt(sockfd[sockfd_used], IPPROTO_IPV6, IPV6_V6ONLY, 
                 (char *) &v6only, sizeof(v6only)) == SOCKET_ERROR)
          Log(1, "servmgr setsockopt (IPV6_V6ONLY): %s", TCPERR());
      }
#endif
      if (setsockopt (sockfd[sockfd_used], SOL_SOCKET, SO_REUSEADDR,
                    (char *) &opt, sizeof opt) == SOCKET_ERROR)
        Log (1, "servmgr setsockopt (SO_REUSEADDR): %s", TCPERR ());
    
      if (bind (sockfd[sockfd_used], ai->ai_addr, ai->ai_addrlen) != 0)
      {
        Log(1, "servmgr bind(): %s", TCPERR ());
        soclose(sockfd[sockfd_used]);
        return -1;
      }
      if (listen (sockfd[sockfd_used], 5) != 0)
      {
        Log(1, "servmgr listen(): %s", TCPERR ());
        soclose(sockfd[sockfd_used]);
        return -1;
      }
#ifdef AMIGA
      /* Every other socket in this codebase gets set non-blocking via
       * setsockopts() (protocol.c, on the accepted session socket) - the
       * *listening* socket itself never did, on any platform, presumably
       * because a real BSD accept() on an already select()-ready
       * listening socket is guaranteed not to block regardless of its
       * own blocking mode. Real-hardware testing found accept() blocking
       * anyway right after a real "select() returned 1" on Amiberry's
       * bsdsocket_emu - either that guarantee doesn't hold here, or
       * something related doesn't. Belt-and-suspenders: make the
       * listening socket itself non-blocking too. */
      setsockopts(sockfd[sockfd_used]);
#endif

      sockfd_used++;
    }

    freeaddrinfo(aiHead);
  }

  if (sockfd_used == 0) {
    Log(1, "servmgr: No listen socket open");
    return -1;
  }

  setproctitle ("server manager (listen %s)", config->listen.first->port);

  for (;;)
  {
    struct timeval tv;
    int n;
    int curfd, maxfd = 0;
    fd_set r;

    FD_ZERO (&r);
    for (curfd=0; curfd<sockfd_used; curfd++)
    {
      FD_SET (sockfd[curfd], &r);
      if (sockfd[curfd] > maxfd)
        maxfd = sockfd[curfd];
    }
    tv.tv_usec = 0;
    tv.tv_sec  = CHECKCFG_INTERVAL;
    unblocksig();
    check_child(&n_servers);
    n = select(maxfd+1, &r, NULL, NULL, &tv);
    blocksig();
    switch (n)
    { case 0: /* timeout */
        if (checkcfg()) 
        {
          for (curfd=0; curfd<sockfd_used; curfd++)
            soclose(sockfd[curfd]);
          sockfd_used = 0;
          return 0;
        }
        unblocksig();
        check_child(&n_servers);
        blocksig();
        continue;
      case -1:
        save_errno = TCPERRNO;
        if (binkd_exit)
          goto accepterr;
        if (TCPERRNO == EINTR)
        {
          unblocksig();
          check_child(&n_servers);
          blocksig();
          if (checkcfg())
          {
            for (curfd=0; curfd<sockfd_used; curfd++)
              soclose(sockfd[curfd]);
            sockfd_used = 0;
            return 0;
          }
          continue;
        }
        Log (1, "servmgr select(): %s", TCPERR ());
        goto accepterr;
    }
 
    for (curfd=0; curfd<sockfd_used; curfd++)
    {
      if (!FD_ISSET(sockfd[curfd], &r))
        continue;

      client_addr_len = sizeof (client_addr);
      if ((new_sockfd = accept (sockfd[curfd], (struct sockaddr *)&client_addr,
                                &client_addr_len)) == INVALID_SOCKET)
      {
        save_errno = TCPERRNO;
#ifdef AMIGA
        /* The listening socket is now explicitly non-blocking (see
         * do_server()'s setsockopts() call after listen()) - accept()
         * can legitimately return EWOULDBLOCK/EAGAIN here (select() said
         * ready, but nothing actually is by the time accept() runs -
         * seen this exact gap on Amiberry's bsdsocket_emu). Not a real
         * error, just try again next time round the loop. */
        if (save_errno == EWOULDBLOCK || save_errno == EAGAIN)
          continue;
#endif
        if (save_errno != EINVAL && save_errno != EINTR)
        {
          if (!binkd_exit)
            Log (1, "servmgr accept(): %s", TCPERR ());
#ifdef UNIX
          if (save_errno == ECONNRESET ||
              save_errno == ETIMEDOUT ||
              save_errno == ECONNABORTED ||
              save_errno == EHOSTUNREACH)
            continue;
#endif
        accepterr:
#ifdef OS2
          /* Buggy external process closed our socket? Or OS/2 bug? */
          if (save_errno == ENOTSOCK)
            return 0;  /* will force socket re-creation */
#endif
          return -1;
        }
      }
      else
      {
        char host[BINKD_FQDNLEN + 1];
        char service[MAXSERVNAME + 1];
        int aiErr;
  
        add_socket(new_sockfd);
        /* Was the socket created after close_sockets loop in exitfunc()? */
        if (binkd_exit)
        {
          del_socket(new_sockfd);
          soclose(new_sockfd);
          continue;
        }
        rel_grow_handles (6);
        ext_rand=rand();
        /* never resolve name in here, will be done during session */
        aiErr = getnameinfo((struct sockaddr *)&client_addr, client_addr_len,
            host, sizeof(host), service, sizeof(service),
            NI_NUMERICHOST | NI_NUMERICSERV);
        if (aiErr == 0) 
          Log (2, "Incoming: %s (%s)", host, service);
        else
        {
          Log(2, "Error in getnameinfo(): %s (%d)", gai_strerror(aiErr), aiErr);
          Log(2, "Incoming: unknown");
        }
  
        /* Creating a new process for the incoming connection */
#ifdef AMIGA
        /* v10.16: hand the accepted socket to the child rather than let it
         * borrow ours.
         *
         * servmgr runs in the Process that opened bsdsocket.library
         * (binkd.c calls servmgr() directly), so accept() here is fine.
         * The child spawned below is a different Process, and until now it
         * used the shared SocketBase for this socket. v10.12 fixed exactly
         * this ambiguity for outbound connect() but reasoned that "the
         * shared select()-based I/O path never depended on signal delivery
         * in the first place" and left inbound alone. That reasoning was
         * wrong: amiga_glue.c's select() is implemented on WaitSelect(),
         * which *is* a signal-wait, and its readiness signals go to
         * whichever Process the library last associated as the caller --
         * not this child. The result was an inbound session that queued
         * its greeting, never flushed a byte, never read the peer's reply,
         * and never timed out, holding the server slot forever and
         * blocking every later connection until a restart.
         *
         * ReleaseSocket() detaches the socket from our SocketBase and
         * returns a transfer id; the child re-materialises it in its own
         * private base with ObtainSocket(). This is bsdsocket.library's
         * documented way to move a socket between Processes. Past this
         * point new_sockfd holds that id, not a descriptor. */
        {
          LONG myid, relid;

          /* 0x51000000 base: just a value no other subsystem on this
           * machine is likely to have released a socket under. */
          threadsafe (myid = 0x51000000 + (++next_handoff_id));
          relid = ReleaseSocket (new_sockfd, myid);

          del_socket (new_sockfd);
          if (relid < 0)
          {
            Log (1, "servmgr ReleaseSocket(): %s", TCPERR ());
            soclose (new_sockfd);
            rel_grow_handles (-6);
            continue;
          }
          /* Use the id we asked for, not the return value: a stack that
           * honours an explicit id returns it unchanged, but do not
           * depend on that. */
          serv_handoff.id     = (int) myid;
          /* Read the family through struct sockaddr: on AMIGA
           * sockaddr_storage is rfc2553.h's sockaddr_in stand-in, which has
           * no ss_family member. */
          serv_handoff.family = ((struct sockaddr *) &client_addr)->sa_family;
          Log (7, "handoff: released fd %i as id %li (family %i), n_servers=%i",
               (int) new_sockfd, (long) relid, serv_handoff.family, n_servers);
        }
        threadsafe(++n_servers);
        if ((pid = branch (serv, (void *) &serv_handoff, sizeof (serv_handoff))) < 0)
        {
          /* Take the socket back so it is not orphaned inside
           * bsdsocket.library with no owner to ever close it. */
          SOCKET back = (SOCKET) ObtainSocket ((LONG) serv_handoff.id,
                                               serv_handoff.family,
                                               SOCK_STREAM, 0);
          if (back != INVALID_SOCKET)
            soclose (back);
          rel_grow_handles (-6);
          threadsafe(--n_servers);
          PostSem(&eothread);
          Log (1, "servmgr branch(): cannot branch out");
          sleep(1);
        }
#else
        threadsafe(++n_servers);
        if ((pid = branch (serv, (void *) &new_sockfd, sizeof (new_sockfd))) < 0)
        {
          del_socket(new_sockfd);
          soclose(new_sockfd);
          rel_grow_handles (-6);
          threadsafe(--n_servers);
          PostSem(&eothread);
          Log (1, "servmgr branch(): cannot branch out");
          sleep(1);
        }
#endif
        else
        {
          Log (5, "started server #%i, id=%i", n_servers, pid);
#if defined(HAVE_FORK) && !defined(HAVE_THREADS)
          soclose (new_sockfd);
#endif
        }
      }
    }
  }
}

void servmgr (void)
{
  int status;
  BINKD_CONFIG *config;

  srand(time(0));
  setproctitle ("server manager");
  Log (4, "servmgr started");

#if defined(HAVE_FORK) && !defined(HAVE_THREADS)
  blocksig();
  signal (SIGCHLD, sighandler);
#endif

  /* Loop on socket (listen can be changed by reload)
   * do_server() will return 0 to restart and -1 to terminate
   */
  do
  {
    config = lock_current_config();
    status = do_server(config);
    unlock_config_structure(config, 0);
  } while (status == 0 && !binkd_exit);
  Log(4, "downing servmgr...");
  pidsmgr = 0;
  PostSem(&eothread);
}
