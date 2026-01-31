/*
 *  client.c -- Outbound calls
 *  Reign of Fire BBS Group FTN-Branded Edition
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#if defined(HAVE_FORK) || defined(WITH_PTHREADS)
#include <signal.h>
#include <sys/wait.h>
#endif

#include "sys.h"
#include "readcfg.h"
#include "client.h"
#include "ftnnode.h"
#include "ftnaddr.h"
#include "common.h"
#include "iptools.h"
#include "ftnq.h"
#include "tools.h"
#include "protocol.h"
#include "bsy.h"
#include "assert.h"
#include "setpttl.h"
#include "sem.h"
#include "run.h"
#if defined(WITH_PERL)
#include "perlhooks.h"
#endif

#ifdef HTTPS
#include "https.h"
#endif
#include "rfc2553.h"
#include "srv_gai.h"

static void call (void *arg);

int n_clients = 0;

#ifdef AF_INET6
#define NO_INVALID_ADDRESSES 2
#else
#define NO_INVALID_ADDRESSES 1
#endif
static struct sockaddr_storage invalidAddresses[NO_INVALID_ADDRESSES];

#if defined(HAVE_FORK) && !defined(HAVE_THREADS)
static void alrm (int signo) { }
#endif

#if defined(HAVE_THREADS)
#define SLEEP(x) WaitSem(&wakecmgr, x)
#else
#define SLEEP(x) sleep(x)
#endif

#if defined(HAVE_THREADS) && defined(OS2)
void rel_grow_handles(int nh)
{
  LONG addfh=0;
  static ULONG curmaxfh=0;

  LockSem(&fhsem);
  if (curmaxfh == 0)
  {
    if (DosSetRelMaxFH(&addfh, &curmaxfh))
    {
      Log(1, "Cannot DosSetRelMaxFH");
      return;
    }
  }
#ifdef __WATCOMC__
  if ((addfh=_grow_handles((int)(curmaxfh += nh))) < curmaxfh)
#else
  addfh=nh;
  if (DosSetRelMaxFH(&addfh, &curmaxfh))
#endif
    Log(1, "Cannot grow handles to %ld (now %ld): %s", curmaxfh, addfh, strerror(errno));
  else
    Log(6, "Set MaxFH to %ld (res %ld)", curmaxfh, addfh);
  ReleaseSem(&fhsem);
}
#endif

struct call_args
{
    FTN_NODE     *node;
    BINKD_CONFIG *config;
};

/*
 * Run one client loop. Return -1 to exit
 */
static int do_client(BINKD_CONFIG *config)
{
  FTN_NODE *r;
  int pid;

  if (!config->q_present)
  {
    q_free (SCAN_LISTED, config);
    if (config->printq)
      Log (-1, "scan\r");
    q_scan (SCAN_LISTED, config);
    config->q_present = 1;
    if (config->printq)
    {
      LockSem (&lsem);
      q_list (stderr, SCAN_LISTED, config);
      ReleaseSem (&lsem);
      Log (-1, "idle\r");
    }
  }

  if (n_clients < config->max_clients)
  {
    if ((r = q_next_node (config)) != 0)
    {
      struct call_args args;

      if (!bsy_test (&r->fa, F_BSY, config) ||
          !bsy_test (&r->fa, F_CSY, config))
      {
        char szDestAddr[FTN_ADDR_SZ + 1];
        ftnaddress_to_str (szDestAddr, &r->fa);

        /* ROF Branding */
        Log (4, "[ROF FTN] Node %s is busy — skipping", szDestAddr);

        return 0;
      }

      rel_grow_handles (6);
      threadsafe(++n_clients);
      lock_config_structure(config);
      args.node   = r;
      args.config = config;

      if ((pid = branch (call, &args, sizeof (args))) < 0)
      {
        unlock_config_structure(config, 0);
        rel_grow_handles (-6);
        threadsafe(--n_clients);
        PostSem(&eothread);
        Log (1, "[ROF FTN] Cannot branch out for outbound call");
        SLEEP(1);
      }
#if !defined(DEBUGCHILD)
      else
      {
        /* ROF Branding */
        Log (5, "[ROF FTN] Outbound client #%i started (pid=%i)", n_clients, pid);

#if defined(HAVE_FORK) && !defined(HAVE_THREADS) && !defined(AMIGA)
        unlock_config_structure(config, 0);
#endif
      }
#endif
    }
    else
    {
      int need_sleep = config->rescan_delay;
      time_t start_sleep = time(NULL), end_sleep;

      unblocksig();
      while (need_sleep > 0 && !binkd_exit
#if defined(HAVE_FORK)
             && !got_sighup
#endif
            )
      {
        check_child(&n_clients);
        if (poll_flag && n_clients <= 0)
        {
          blocksig();
          if (q_not_empty(config) == 0)
          {
            Log (4, "[ROF FTN] Queue empty — clientmgr exiting");
            return -1;
          }
          unblocksig();
        }
        SLEEP (need_sleep);
        end_sleep = time(NULL);
        if (end_sleep > start_sleep)
          need_sleep -= (int)(end_sleep - start_sleep);
        start_sleep = end_sleep;
      }
      check_child(&n_clients);
      blocksig();
      if (!poll_flag)
        config->q_present = 0;
    }
  }
  else
  {
    unblocksig();
    check_child(&n_clients);
    SLEEP (config->call_delay);
    check_child(&n_clients);
    blocksig();
  }

  return 0;
}

void clientmgr (void *arg)
{
  int status;
  BINKD_CONFIG *config = NULL;
#if defined(WITH_PERL) && defined(HAVE_THREADS)
  void *cperl = NULL;
#endif

  UNUSED_ARG(arg);

  /* ROF Branding */
  Log (4, "============================================================");
  Log (4, "***  REIGN OF FIRE BBS GROUP — FTN OUTBOUND MANAGER  ***");
  Log (4, "============================================================");

  /* Initialize invalid addresses */
  ((struct sockaddr *)&invalidAddresses[0])->sa_family = AF_INET;
#if defined(AF_INET6) && defined(_SS_MAXSIZE)
  invalidAddresses[1].ss_family = AF_INET6;
#endif

#ifdef HAVE_THREADS
  pidcmgr = PID();
#elif defined(HAVE_FORK)
  pidcmgr = 0;
  pidCmgr = (int) getpid();
  blocksig();
  signal (SIGCHLD, sighandler);
#endif

  config = lock_current_config();
#if defined(WITH_PERL) && defined(HAVE_THREADS)
  if (server_flag)
    cperl = perl_init_clone(config);
#endif

#ifndef HAVE_THREADS
  setproctitle ("client manager");
#endif

  Log (4, "[ROF FTN] clientmgr started");

  for (;;)
  {
    if (config != current_config)
    {
#if defined(WITH_PERL) && defined(HAVE_THREADS)
      if (server_flag && cperl)
        perl_done_clone(cperl);
#endif
      if (config)
        unlock_config_structure(config, 0);
      config = lock_current_config();
#if defined(WITH_PERL) && defined(HAVE_THREADS)
      if (server_flag)
        cperl = perl_init_clone(config);
#endif
    }

    status = do_client(config);

    if (status != 0 || binkd_exit)
      break;

#ifdef HAVE_THREADS
    if (!server_flag && !poll_flag)
      checkcfg();
#else
    if (!poll_flag)
      checkcfg();
#endif
  }

  Log (5, "[ROF FTN] Shutting down clientmgr...");

#if defined(WITH_PERL) && defined(HAVE_THREADS)
  if (server_flag && cperl)
    perl_done_clone(cperl);
#endif

  unlock_config_structure(config, 0);

  unblocksig();
#ifdef HAVE_THREADS
  pidcmgr = 0;
  if (server_flag) {
    PostSem(&eothread);
    if (binkd_exit)
      ENDTHREAD();
  }
#endif

  exit (0);
}

static int call0 (FTN_NODE *node, BINKD_CONFIG *config)
{
  int sockfd = INVALID_SOCKET;
  int sock_out;
  char szDestAddr[FTN_ADDR_SZ + 1];
  int i, j, rc, pid = -1;
  char host[BINKD_FQDNLEN + 5 + 1];
  char addrbuf[BINKD_FQDNLEN + 1];
  char servbuf[MAXSERVNAME + 1];
  char *hosts;
  char port[MAXPORTSTRLEN + 1] = { 0 };
  char *dst_ip = NULL;
  const char *save_err;

#ifdef HTTPS
  int use_proxy;
  char *proxy, *socks;
  struct addrinfo *aiProxyHead;
#endif

  struct addrinfo *ai, *aiNodeHead, *aiHead, hints;

#ifdef AF_FORCE
  struct addrinfo *aiNewHead;
#endif

  int aiErr;

  memset((void *)&hints, 0, sizeof(hints));
  hints.ai_family = node->IP_afamily;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

#ifdef WITH_PERL
  hosts = xstrdup(node->hosts);
#ifdef HTTPS
  proxy = xstrdup(config->proxy);
  socks = xstrdup(config->socks);
#endif
  if (!perl_on_call(node, config, &hosts
#ifdef HTTPS
                    , &proxy, &socks
#endif
                    )) {
    Log(1, "[ROF FTN] call aborted by Perl on_call()");
    return 0;
  }
#else
  hosts = node->hosts;
#ifdef HTTPS
  proxy = config->proxy;
  socks = config->socks;
#endif
#endif

  ftnaddress_to_str (szDestAddr, &node->fa);

  /* ROF Branding */
  Log (2, "[ROF FTN] Outbound call to %s", szDestAddr);

#ifndef HAVE_THREADS
  setproctitle ("call to %s", szDestAddr);
#endif

#ifdef HTTPS
  use_proxy = (node->NP_flag != NP_ON) && (!node->pipe || !node->pipe[0]) && (proxy[0] || socks[0]);
  if (use_proxy)
  {
    char *sp, *sport;
    strncpy(host, proxy[0] ? proxy : socks, sizeof(host));
    if ((sp=strchr(host, ':')) != NULL)
    {
      *sp++ = '\0';
      sport = sp;
      if ((sp=strchr(sp, '/')) != NULL)
        *sp++ = '\0';
    }
    else
    {
      if ((sp=strchr(host, '/')) != NULL)
        *sp++ = '\0';
      sport = proxy[0] ? "squid" : "socks";
    }

    if ( (aiErr = srv_getaddrinfo(host, sport, &hints, &aiProxyHead)) != 0)
    {
        Log(2, "[ROF FTN] Proxy port %s not found, trying default", sp);
        aiErr = getaddrinfo(host, proxy[0] ? "3128" : "1080", &hints, &aiProxyHead);
    }

    if (aiErr != 0)
    {
      Log(1, "[ROF FTN] Proxy/Socks host %s not found", host);
#ifdef WITH_PERL
      xfree(hosts);
      xfree(proxy);
      xfree(socks);
#endif
      return 0;
    }
  }
#endif

  for (i = 1; sockfd == INVALID_SOCKET
       && (rc = get_host_and_port
           (i, host, port, hosts, &node->fa, config)) != -1; ++i)
  {
    if (rc == 0)
    {
      Log (1, "[ROF FTN] Error parsing host list: %s (%i)", hosts, i);
      continue;
    }

    pid = -1;

    if (node->pipe && node->pipe[0])
    {
      char *cmdline = strdup(node->pipe);
      cmdline = ed(cmdline, "*H", host, NULL);
      cmdline = ed(cmdline, "*I", port, NULL);
      pid = run3(cmdline, &sock_out, &sockfd, NULL);
      free(cmdline);

      if (pid != -1)
      {
        Log (4, "[ROF FTN] Connected via external pipe");
        add_socket(sock_out);
        break;
      }

      if (!binkd_exit)
        Log (1, "[ROF FTN] External pipe connection failed");

      sockfd = INVALID_SOCKET;
      continue;
    }

#ifdef HTTPS
    if (use_proxy)
      aiHead = aiProxyHead;
    else
#endif
    {
      aiErr = srv_getaddrinfo(host, port, &hints, &aiNodeHead);

      if (aiErr != 0)
      {
        Log(2, "[ROF FTN] getaddrinfo failed: %s (%d)", gai_strerror(aiErr), aiErr);
        bad_try(&node->fa, "Cannot getaddrinfo", BAD_CALL, config);
        continue;
      }

      aiHead = aiNodeHead;
    }

#ifdef AF_INET6
#ifdef AF_FORCE
    if (aiHead->ai_family == AF_INET && node->AFF_flag == 6)
    {
       for (ai = aiHead; ai != NULL; ai = ai->ai_next)
       {
          if (ai->ai_family == AF_INET && ai->ai_next != NULL && ai->ai_next->ai_family == AF_INET6)
          {
             aiNewHead = ai->ai_next;
             ai->ai_next = aiNewHead->ai_next;
             aiNewHead->ai_next = aiHead;
             aiHead = aiNewHead;
             break;
          }
       }
    }
    else if (aiHead->ai_family == AF_INET6 && node->AFF_flag == 4)
    {
       for (ai = aiHead; ai != NULL; ai = ai->ai_next)
       {
          if (ai->ai_family == AF_INET6 && ai->ai_next != NULL && ai->ai_next->ai_family == AF_INET)
          {
             aiNewHead = ai->ai_next;
             ai->ai_next = aiNewHead->ai_next;
             aiNewHead->ai_next = aiHead;
             aiHead = aiNewHead;
             break;
          }
       }
    }
#endif
#endif

    for (ai = aiHead; ai != NULL && sockfd == INVALID_SOCKET; ai = ai->ai_next)
    {
      for (j = 0; j < NO_INVALID_ADDRESSES; j++)
        if (0 == sockaddr_cmp_addr(ai->ai_addr, (struct sockaddr *)&invalidAddresses[j]))
        {
          const int l =
#if defined(AF_INET6) && defined(_SS_MAXSIZE)
            invalidAddresses[j].ss_family == AF_INET6 ?
            sizeof(struct sockaddr_in6) :
#endif
            sizeof(struct sockaddr_in);

          rc = getnameinfo((struct sockaddr *)&invalidAddresses[j], l,
                           addrbuf, sizeof(addrbuf),
                           NULL, 0, NI_NUMERICHOST);

          if (rc != 0)
            Log(2, "[ROF FTN] getnameinfo error: %s (%d)", gai_strerror(rc), rc);
          else
            Log(1, "[ROF FTN] Invalid address: %s", addrbuf);

          break;
        }

      if (j < NO_INVALID_ADDRESSES)
        continue;

      if ((sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == INVALID_SOCKET)
      {
        Log (1, "[ROF FTN] socket error: %s", TCPERR());
        continue;
      }

      add_socket(sockfd);

      if (binkd_exit)
      {
#ifdef WITH_PERL
        xfree(hosts);
#ifdef HTTPS
        xfree(proxy);
        xfree(socks);
#endif
#endif
        freeaddrinfo(aiHead);
        return 0;
      }

      rc = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                       addrbuf, sizeof(addrbuf),
                       servbuf, sizeof(servbuf),
                       NI_NUMERICHOST | NI_NUMERICSERV);

      if (rc != 0)
      {
        Log (2, "[ROF FTN] getnameinfo error: %s (%d)", gai_strerror(rc), rc);
        snprintf(addrbuf, BINKD_FQDNLEN, "invalid");
        *servbuf = '\0';
      }

#ifdef HTTPS
      if (use_proxy)
      {
        char *sp = strchr(host, ':');
        if (sp) *sp = '\0';

        if (strcmp(port, config->oport) == 0)
          Log (4, "[ROF FTN] Trying %s via proxy %s:%s...", host, addrbuf, servbuf);
        else
          Log (4, "[ROF FTN] Trying %s:%s via proxy %s:%s...", host, port, addrbuf, servbuf);
      }
      else
#endif
      {
        if (strcmp(port, config->oport) == 0)
          Log (4, "[ROF FTN] Trying %s [%s]...", host, addrbuf);
        else
          Log (4, "[ROF FTN] Trying %s [%s]:%s...", host, addrbuf, servbuf);

        dst_ip = addrbuf;
        strnzcpy(port, servbuf, MAXPORTSTRLEN);
      }

      if (config->bindaddr[0])
      {
        struct addrinfo
