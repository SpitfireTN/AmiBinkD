#include "rfc2553.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "sem.h"

/* ----------------------------------------------------------------------
 *  This file provides RFC2553-compatible getaddrinfo(), freeaddrinfo(),
 *  gai_strerror(), and getnameinfo() for systems (like AmigaOS) that
 *  lack native support.
 *
 *  Fully rewritten for:
 *    - C89 compatibility
 *    - Amiga bsdsocket.library behavior
 *    - Strict RFC2553 semantics (IPv4 only)
 *    - No undefined behavior
 *    - Safe memory handling
 * ---------------------------------------------------------------------- */

#ifndef HAVE_GETADDRINFO

/* ----------------------------------------------------------------------
 *  getaddrinfo()
 * ---------------------------------------------------------------------- */
int getaddrinfo(const char *nodename,
                const char *servname,
                const struct addrinfo *hints,
                struct addrinfo **res)
{
    struct addrinfo *head = NULL;
    struct addrinfo *tail = NULL;
    struct hostent *he = NULL;
    unsigned long port = 0;
    int socktype = SOCK_STREAM;
    int protocol = 0;
    int passive = 0;
    int ret = 0;

    char *endptr;

    if (!res)
        return EAI_FAIL;

    *res = NULL;

    /* Validate hints */
    if (hints) {
        if (hints->ai_family != AF_UNSPEC &&
            hints->ai_family != AF_INET)
            return EAI_FAMILY;

        if (hints->ai_socktype != 0)
            socktype = hints->ai_socktype;

        passive = (hints->ai_flags & AI_PASSIVE);
    }

    /* Parse service */
    if (servname) {
        port = strtoul(servname, &endptr, 10);
        if (*endptr != '\0') {
            /* Not numeric: lookup service name */
            struct servent *se = NULL;

            lockresolvsem();
            if (socktype == SOCK_DGRAM)
                se = getservbyname(servname, "udp");
            else
                se = getservbyname(servname, "tcp");
            if (se) {
                port = ntohs(se->s_port);
                protocol = (strcmp(se->s_proto, "udp") == 0)
                           ? IPPROTO_UDP : IPPROTO_TCP;
            } else {
                releaseresolvsem();
                return EAI_SERVICE;
            }
            releaseresolvsem();
        }
    }

    /* Determine protocol if not set */
    if (protocol == 0) {
        if (socktype == SOCK_DGRAM)
            protocol = IPPROTO_UDP;
        else
            protocol = IPPROTO_TCP;
    }

    /* Host lookup unless passive */
    if (!passive) {
        if (!nodename)
            return EAI_NONAME;

        lockresolvsem();
        he = gethostbyname(nodename);
        if (!he) {
            int err = h_errno;
            releaseresolvsem();

            if (err == TRY_AGAIN) return EAI_AGAIN;
            if (err == NO_RECOVERY) return EAI_FAIL;
            return EAI_NONAME;
        }
        releaseresolvsem();
    }

    /* Build addrinfo list */
    {
        char **addrlist;
        struct in_addr *ina;

        if (passive) {
            /* Passive: single entry with INADDR_ANY */
            struct addrinfo *ai =
                (struct addrinfo *)calloc(1, sizeof(struct addrinfo));
            struct sockaddr_in *sa =
                (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));

            if (!ai || !sa) {
                free(ai);
                free(sa);
                return EAI_MEMORY;
            }

            sa->sin_family = AF_INET;
            sa->sin_port = htons((unsigned short)port);
            sa->sin_addr.s_addr = htonl(INADDR_ANY);

            ai->ai_family = AF_INET;
            ai->ai_socktype = socktype;
            ai->ai_protocol = protocol;
            ai->ai_addrlen = sizeof(struct sockaddr_in);
            ai->ai_addr = (struct sockaddr *)sa;

            *res = ai;
            return 0;
        }

        /* Normal lookup */
        addrlist = he->h_addr_list;

        while (*addrlist) {
            struct addrinfo *ai =
                (struct addrinfo *)calloc(1, sizeof(struct addrinfo));
            struct sockaddr_in *sa =
                (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));

            if (!ai || !sa) {
                free(ai);
                free(sa);
                ret = EAI_MEMORY;
                break;
            }

            ina = (struct in_addr *)(*addrlist);

            sa->sin_family = AF_INET;
            sa->sin_port = htons((unsigned short)port);
            memcpy(&sa->sin_addr, ina, sizeof(struct in_addr));

            ai->ai_family = AF_INET;
            ai->ai_socktype = socktype;
            ai->ai_protocol = protocol;
            ai->ai_addrlen = sizeof(struct sockaddr_in);
            ai->ai_addr = (struct sockaddr *)sa;

            if (!head)
                head = ai;
            else
                tail->ai_next = ai;

            tail = ai;
            addrlist++;
        }
    }

    if (ret != 0) {
        if (head)
            freeaddrinfo(head);
        return ret;
    }

    *res = head;
    return 0;
}

/* ----------------------------------------------------------------------
 *  freeaddrinfo()
 * ---------------------------------------------------------------------- */
void freeaddrinfo(struct addrinfo *ai)
{
    struct addrinfo *next;

    while (ai) {
        next = ai->ai_next;
        free(ai->ai_addr);
        free(ai);
        ai = next;
    }
}

/* ----------------------------------------------------------------------
 *  gai_strerror()
 * ---------------------------------------------------------------------- */
static const char *ai_errlist[] = {
    "Success",
    "Name or service not known",
    "Temporary failure in name resolution",
    "Non-recoverable failure in name resolution",
    "No data available",
    "Address family not supported",
    "Socket type not supported",
    "Service not supported for socket type",
    "Address family not supported by hostname",
    "Memory allocation failure",
    "System error",
    "Unknown error"
};

char *gai_strerror(int ecode)
{
    int idx = -ecode;

    if (idx < 0 || idx >= (int)(sizeof(ai_errlist)/sizeof(ai_errlist[0])))
        idx = sizeof(ai_errlist)/sizeof(ai_errlist[0]) - 1;

    return (char *)ai_errlist[idx];
}

#endif /* HAVE_GETADDRINFO */


#ifndef HAVE_GETNAMEINFO

/* ----------------------------------------------------------------------
 *  getnameinfo()
 * ---------------------------------------------------------------------- */
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen,
                int flags)
{
    const struct sockaddr_in *sin =
        (const struct sockaddr_in *)sa;

    if (sa->sa_family != AF_INET)
        return EAI_FAMILY;

    /* Hostname */
    if (host && hostlen > 0) {
        if (!(flags & NI_NUMERICHOST)) {
            struct hostent *he;

            lockresolvsem();
            he = gethostbyaddr((const char *)&sin->sin_addr,
                               sizeof(struct in_addr),
                               AF_INET);
            if (he) {
                strncpy(host, he->h_name, hostlen - 1);
                host[hostlen - 1] = '\0';
            } else {
                if (flags & NI_NAMEREQD) {
                    releaseresolvsem();
                    return EAI_NONAME;
                }
                flags |= NI_NUMERICHOST;
            }
            releaseresolvsem();
        }

        if (flags & NI_NUMERICHOST) {
            lockhostsem();
            strncpy(host, inet_ntoa(sin->sin_addr), hostlen - 1);
            host[hostlen - 1] = '\0';
            releasehostsem();
        }
    }

    /* Service */
    if (serv && servlen > 0) {
        if (!(flags & NI_NUMERICSERV)) {
            struct servent *se;

            lockresolvsem();
            if (flags & NI_DATAGRAM)
                se = getservbyport(ntohs(sin->sin_port), "udp");
            else
                se = getservbyport(ntohs(sin->sin_port), "tcp");

            if (se) {
                strncpy(serv, se->s_name, servlen - 1);
                serv[servlen - 1] = '\0';
            } else {
                if (flags & NI_NAMEREQD) {
                    releaseresolvsem();
                    return EAI_NONAME;
                }
                flags |= NI_NUMERICSERV;
            }
            releaseresolvsem();
        }

        if (flags & NI_NUMERICSERV) {
            snprintf(serv, servlen, "%u",
                     (unsigned)ntohs(sin->sin_port));
        }
    }

    return 0;
}

#endif /* HAVE_GETNAMEINFO */
