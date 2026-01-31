/*
 * Modernized server manager for AmiBinkd v9.10
 * ---------------------------------------------------------------
 * - Fully compatible with original behavior
 * - Safe socket lifecycle
 * - Clean getaddrinfo() / getnameinfo() usage
 * - Predictable reload logic
 * - Unified error handling
 * - Thread/fork compatible
 * - IPv4/IPv6 aware (via RFC2553 shim)
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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

/* ---------------------------------------------------------------------- */
/* Globals                                                                */
/* ---------------------------------------------------------------------- */

int n_servers = 0;
int ext_rand  = 0;

SOCKET sockfd[MAX_LISTENSOCK];
int sockfd_used = 0;

/* ---------------------------------------------------------------------- */
/* Worker thread / child process                                          */
/* ---------------------------------------------------------------------- */

static void serv(void *arg)
{
    int h = *(int *)arg;
    free(arg);

    BINKD_CONFIG *config = lock_current_config();

#if defined(WITH_PERL) && defined(HAVE_THREADS)
    void *cperl = perl_init_clone(config);
#endif

#if defined(HAVE_FORK) && !defined(HAVE_THREADS) && !defined(DEBUGCHILD)
    /* Close inherited listening sockets */
    for (int i = 0; i < sockfd_used; i++)
        soclose(sockfd[i]);
#endif

    protocol(h, h, NULL, NULL, NULL, NULL, NULL, config);

    Log(5, "downing server...");

#if defined(WITH_PERL) && defined(HAVE_THREADS)
    perl_done_clone(cperl);
#endif

    del_socket(h);
    soclose(h);

    unlock_config_structure(config, 0);
    rel_grow_handles(-6);

#ifdef HAVE_THREADS
    threadsafe(--n_servers);
    PostSem(&eothread);
    ENDTHREAD();
#elif defined(DOS) || defined(DEBUGCHILD)
    --n_servers;
#endif
}

/* ---------------------------------------------------------------------- */
/* Helper: create and bind a single listening socket                      */
/* ---------------------------------------------------------------------- */

static int create_listen_socket(struct addrinfo *ai)
{
    SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s < 0)
    {
        Log(1, "servmgr socket(): %s", TCPERR());
        return INVALID_SOCKET;
    }

#ifdef UNIX
    if (fcntl(s, F_SETFD, FD_CLOEXEC) != 0)
        Log(1, "servmgr fcntl(FD_CLOEXEC): %s", strerror(errno));
#endif

#ifdef IPV6_V6ONLY
    if (ai->ai_family == AF_INET6)
    {
        int v6only = 1;
        if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
                       (char *)&v6only, sizeof(v6only)) == SOCKET_ERROR)
            Log(1, "servmgr setsockopt(IPV6_V6ONLY): %s", TCPERR());
    }
#endif

    int opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&opt, sizeof(opt)) == SOCKET_ERROR)
        Log(1, "servmgr setsockopt(SO_REUSEADDR): %s", TCPERR());

    if (bind(s, ai->ai_addr, ai->ai_addrlen) != 0)
    {
        Log(1, "servmgr bind(): %s", TCPERR());
        soclose(s);
        return INVALID_SOCKET;
    }

    if (listen(s, 5) != 0)
    {
        Log(1, "servmgr listen(): %s", TCPERR());
        soclose(s);
        return INVALID_SOCKET;
    }

    return s;
}

/* ---------------------------------------------------------------------- */
/* Main server loop                                                       */
/* ---------------------------------------------------------------------- */

static int do_server(BINKD_CONFIG *config)
{
    struct addrinfo hints, *aiHead, *ai;
    struct listenchain *lc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_family   = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    sockfd_used = 0;

    /* Build all listening sockets */
    for (lc = config->listen.first; lc; lc = lc->next)
    {
        int aiErr = getaddrinfo(
            lc->addr[0] ? lc->addr : NULL,
            lc->port,
            &hints,
            &aiHead
        );

        if (aiErr != 0)
        {
            Log(1, "servmgr getaddrinfo: %s (%d)", gai_strerror(aiErr), aiErr);
            return -1;
        }

        for (ai = aiHead; ai && sockfd_used < MAX_LISTENSOCK; ai = ai->ai_next)
        {
            SOCKET s = create_listen_socket(ai);
            if (s == INVALID_SOCKET)
            {
                freeaddrinfo(aiHead);
                return -1;
            }

            sockfd[sockfd_used++] = s;
        }

        Log(3, "servmgr listen on %s:%s",
            lc->addr[0] ? lc->addr : "*",
            lc->port);

        freeaddrinfo(aiHead);
    }

    if (sockfd_used == 0)
    {
        Log(1, "servmgr: No listen socket open");
        return -1;
    }

    setproctitle("server manager (listen %s)", config->listen.first->port);

    /* Main accept loop */
    for (;;)
    {
        fd_set r;
        FD_ZERO(&r);

        int maxfd = 0;
        for (int i = 0; i < sockfd_used; i++)
        {
            FD_SET(sockfd[i], &r);
            if (sockfd[i] > maxfd)
                maxfd = sockfd[i];
        }

        struct timeval tv = { CHECKCFG_INTERVAL, 0 };

        unblocksig();
        check_child(&n_servers);
        int n = select(maxfd + 1, &r, NULL, NULL, &tv);
        blocksig();

        if (n == 0)
        {
            if (checkcfg())
            {
                for (int i = 0; i < sockfd_used; i++)
                    soclose(sockfd[i]);
                sockfd_used = 0;
                return 0;
            }
            continue;
        }

        if (n < 0)
        {
            int e = TCPERRNO;
            if (binkd_exit)
                return -1;

            if (e == EINTR)
            {
                if (checkcfg())
                {
                    for (int i = 0; i < sockfd_used; i++)
                        soclose(sockfd[i]);
                    sockfd_used = 0;
                    return 0;
                }
                continue;
            }

            Log(1, "servmgr select(): %s", TCPERR());
            return -1;
        }

        /* Accept on all ready sockets */
        for (int i = 0; i < sockfd_used; i++)
        {
            if (!FD_ISSET(sockfd[i], &r))
                continue;

            struct sockaddr_storage client_addr;
            socklen_t client_len = sizeof(client_addr);

            SOCKET ns = accept(sockfd[i],
                               (struct sockaddr *)&client_addr,
                               &client_len);

            if (ns == INVALID_SOCKET)
            {
                int e = TCPERRNO;

#ifdef OS2
                if (e == ENOTSOCK)
                    return 0;
#endif

#ifdef UNIX
                if (e == ECONNRESET || e == ETIMEDOUT ||
                    e == ECONNABORTED || e == EHOSTUNREACH)
                    continue;
#endif

                if (!binkd_exit)
                    Log(1, "servmgr accept(): %s", TCPERR());
                return -1;
            }

            /* Log incoming connection */
            char host[BINKD_FQDNLEN + 1];
            char service[MAXSERVNAME + 1];

            int aiErr = getnameinfo(
                (struct sockaddr *)&client_addr,
                client_len,
                host, sizeof(host),
                service, sizeof(service),
                NI_NUMERICHOST | NI_NUMERICSERV
            );

            if (aiErr == 0)
                Log(3, "incoming from %s (%s)", host, service);
            else
                Log(3, "incoming from unknown");

            add_socket(ns);

            if (binkd_exit)
            {
                del_socket(ns);
                soclose(ns);
                continue;
            }

            rel_grow_handles(6);
            ext_rand = rand();

            threadsafe(++n_servers);

            int *arg = xalloc(sizeof(int));
            *arg = ns;

            int pid = branch(serv, arg, sizeof(int));
            if (pid < 0)
            {
                del_socket(ns);
                soclose(ns);
                rel_grow_handles(-6);
                threadsafe(--n_servers);
                PostSem(&eothread);
                Log(1, "servmgr branch(): cannot branch out");
                sleep(1);
            }
            else
            {
                Log(5, "started server #%i, id=%i", n_servers, pid);

#if defined(HAVE_FORK) && !defined(HAVE_THREADS)
                soclose(ns);
#endif
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Entry point                                                            */
/* ---------------------------------------------------------------------- */

void servmgr(void)
{
    srand(time(0));
    setproctitle("server manager");
    Log(4, "servmgr started");

#if defined(HAVE_FORK) && !defined(HAVE_THREADS)
    blocksig();
    signal(SIGCHLD, sighandler);
#endif

    int status;
    do
    {
        BINKD_CONFIG *config = lock_current_config();
        status = do_server(config);
        unlock_config_structure(config, 0);
    }
    while (status == 0 && !binkd_exit);

    Log(4, "downing servmgr...");
    pidsmgr = 0;
    PostSem(&eothread);
}
