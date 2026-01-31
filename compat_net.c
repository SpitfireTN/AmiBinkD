/*
 * compat_net.c
 *
 * Drop-in portability helpers:
 *  - getaddrinfo / freeaddrinfo / getnameinfo / gai_strerror shims
 *  - SRV-aware resolver: srv_getaddrinfo()
 *  - snprintf / vsnprintf fallback
 *
 * Add this file to your build; no existing sources need to be edited.
 */

#include "rfc2553.h"
#include "srv_gai.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
# include <windns.h>
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <resolv.h>
# include <arpa/nameser.h>
#endif

#include "sem.h"
#include "sys.h"
#include "common.h"
#include "iphdr.h"

/* ---------------------------------------------------------------------- */
/* 1. RFC2553 shims: getaddrinfo / freeaddrinfo / getnameinfo / gai_strerror */
/* ---------------------------------------------------------------------- */

#ifndef HAVE_GETADDRINFO

int getaddrinfo(const char *nodename, const char *servname,
                const struct addrinfo *hints,
                struct addrinfo **res)
{
    struct addrinfo **Result = res;
    struct hostent *Addr;
    unsigned int Port;
    int Proto;
    const char *End;
    char **CurAddr;
    int ret = 0;

    if (res == NULL)
        return EAI_UNKNOWN;
    *res = NULL;

    Port  = servname ? htons(strtol(servname, (char **)&End, 0)) : 0;
    Proto = SOCK_STREAM;

    if (hints != NULL && hints->ai_socktype != 0)
        Proto = hints->ai_socktype;

    lockresolvsem();

    if (servname != NULL && End != servname + strlen(servname))
    {
        struct servent *Srv = NULL;

        if (hints == 0 || hints->ai_socktype == SOCK_STREAM)
            Srv = getservbyname(servname, "tcp");
        if (hints != 0 && hints->ai_socktype == SOCK_DGRAM)
            Srv = getservbyname(servname, "udp");
        if (Srv == 0)
        {
            ret = EAI_NONAME;
            goto cleanup;
        }

        Port = Srv->s_port;
        if (strcmp(Srv->s_proto, "tcp") == 0)
            Proto = SOCK_STREAM;
        else if (strcmp(Srv->s_proto, "udp") == 0)
            Proto = SOCK_DGRAM;
        else
        {
            ret = EAI_NONAME;
            goto cleanup;
        }

        if (hints != 0 && hints->ai_socktype != Proto &&
            hints->ai_socktype != 0)
        {
            ret = EAI_SERVICE;
            goto cleanup;
        }
    }

    if (hints != 0 && (hints->ai_flags & AI_PASSIVE) != AI_PASSIVE)
    {
        Addr = gethostbyname(nodename);
        if (Addr == 0)
        {
            if (h_errno == TRY_AGAIN)
            {
                ret = EAI_AGAIN;
                goto cleanup;
            }
            if (h_errno == NO_RECOVERY)
            {
                ret = EAI_FAIL;
                goto cleanup;
            }
            ret = EAI_NONAME;
            goto cleanup;
        }

        if (Addr->h_addr_list[0] == 0)
        {
            ret = EAI_NONAME;
            goto cleanup;
        }

        CurAddr = Addr->h_addr_list;
    }
    else
        CurAddr = (char **)&End; /* fake */

    for (; *CurAddr != NULL; CurAddr++)
    {
        *Result = (struct addrinfo *)calloc(sizeof(**Result), 1);
        if (*Result == NULL)
        {
            ret = EAI_MEMORY;
            goto cleanup;
        }
        if (*res == NULL)
            *res = *Result;

        (*Result)->ai_family   = AF_INET;
        (*Result)->ai_socktype = Proto;

#ifdef IPPROTO_TCP
        if (Proto == SOCK_STREAM)
            (*Result)->ai_protocol = IPPROTO_TCP;
        if (Proto == SOCK_DGRAM)
            (*Result)->ai_protocol = IPPROTO_UDP;
#endif

        (*Result)->ai_addrlen = sizeof(struct sockaddr_in);
        (*Result)->ai_addr    = (struct sockaddr *)calloc(sizeof(struct sockaddr_in), 1);
        if ((*Result)->ai_addr == 0)
        {
            ret = EAI_MEMORY;
            goto cleanup;
        }

        ((struct sockaddr_in *)(*Result)->ai_addr)->sin_family = AF_INET;
        ((struct sockaddr_in *)(*Result)->ai_addr)->sin_port   = Port;

        if (hints != 0 && (hints->ai_flags & AI_PASSIVE) != AI_PASSIVE)
            ((struct sockaddr_in *)(*Result)->ai_addr)->sin_addr =
                *(struct in_addr *)(*CurAddr);
        else
        {
            /* already zeroed by calloc */
            break;
        }

        Result = &(*Result)->ai_next;
    }

cleanup:
    releaseresolvsem();
    if (ret != 0 && *res != NULL)
    {
        freeaddrinfo(*res);
        *res = NULL;
    }

    return ret;
}

void freeaddrinfo(struct addrinfo *ai)
{
    struct addrinfo *Tmp;
    while (ai != 0)
    {
        free(ai->ai_addr);
        Tmp = ai;
        ai  = ai->ai_next;
        free(Tmp);
    }
}

static char *ai_errlist[] = {
    "Success",
    "hostname nor servname provided, or not known", /* EAI_NONAME     */
    "Temporary failure in name resolution",         /* EAI_AGAIN      */
    "Non-recoverable failure in name resolution",   /* EAI_FAIL       */
    "No address associated with hostname",          /* EAI_NODATA     */
    "ai_family not supported",                      /* EAI_FAMILY     */
    "ai_socktype not supported",                    /* EAI_SOCKTYPE   */
    "service name not supported for ai_socktype",   /* EAI_SERVICE    */
    "Address family for hostname not supported",    /* EAI_ADDRFAMILY */
    "Memory allocation failure",                    /* EAI_MEMORY     */
    "System error returned in errno",               /* EAI_SYSTEM     */
    "Unknown error",                                /* EAI_UNKNOWN    */
};

char *gai_strerror(int ecode)
{
    if (ecode > EAI_NONAME || ecode < EAI_UNKNOWN)
        ecode = EAI_UNKNOWN;
    return ai_errlist[-ecode];
}

#endif /* !HAVE_GETADDRINFO */

#ifndef HAVE_GETNAMEINFO

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen,
                int flags)
{
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;

    if (sa->sa_family != AF_INET)
        return EAI_ADDRFAMILY;

    if (host != 0)
    {
        if ((flags & NI_NUMERICHOST) != NI_NUMERICHOST)
        {
            struct hostent *Ent;

            lockresolvsem();
            Ent = gethostbyaddr((char *)&sin->sin_addr,
                                sizeof(sin->sin_addr), AF_INET);
            if (Ent != 0)
                strncpy(host, Ent->h_name, hostlen);
            else
            {
                if ((flags & NI_NAMEREQD) == NI_NAMEREQD)
                {
                    if (h_errno == TRY_AGAIN)
                    {
                        releaseresolvsem();
                        return EAI_AGAIN;
                    }
                    if (h_errno == NO_RECOVERY)
                    {
                        releaseresolvsem();
                        return EAI_FAIL;
                    }
                    releaseresolvsem();
                    return EAI_NONAME;
                }
                flags |= NI_NUMERICHOST;
            }
            releaseresolvsem();
        }

        if ((flags & NI_NUMERICHOST) == NI_NUMERICHOST)
        {
            lockhostsem();
            strncpy(host, inet_ntoa(sin->sin_addr), hostlen);
            releasehostsem();
        }
    }

    if (serv != 0)
    {
        if ((flags & NI_NUMERICSERV) != NI_NUMERICSERV)
        {
            struct servent *Ent;
            lockresolvsem();
            if ((flags & NI_DATAGRAM) == NI_DATAGRAM)
                Ent = getservbyport(ntohs(sin->sin_port), "udp");
            else
                Ent = getservbyport(ntohs(sin->sin_port), "tcp");

            if (Ent != 0)
                strncpy(serv, Ent->s_name, servlen);
            else
            {
                if ((flags & NI_NAMEREQD) == NI_NAMEREQD)
                {
                    releaseresolvsem();
                    return EAI_NONAME;
                }
                flags |= NI_NUMERICSERV;
            }
            releaseresolvsem();
        }

        if ((flags & NI_NUMERICSERV) == NI_NUMERICSERV)
            snprintf(serv, servlen, "%u", ntohs(sin->sin_port));
    }

    return 0;
}

#endif /* !HAVE_GETNAMEINFO */

/* ---------------------------------------------------------------------- */
/* 2. SRV-aware resolver: srv_getaddrinfo()                               */
/* ---------------------------------------------------------------------- */

int srv_getaddrinfo(const char *node, const char *service,
                    const struct addrinfo *hints,
                    struct addrinfo **res)
{
    char *srv_name;
    size_t srv_name_size;
    char tgt_port[6];
#ifdef WIN32
    PDNS_RECORD resp, entry;
    char *tgt_name;
#else
    char tgt_name[BINKD_FQDNLEN + 1];
    unsigned char resp[SRVGAI_DNSRESPLEN];
    ns_msg nsb;
    ns_rr rrb;
    int rlen, i, rrlen;
    const unsigned char *p;
    struct in_addr dummy_addr;
#endif
    int rc;
    struct addrinfo *ai, **ai_last = res;

    if (!node || !*node || !service || !*service || !hints || !res)
        return getaddrinfo(node, service, hints, res);

    if (hints->ai_flags & AI_NUMERICHOST)
        return getaddrinfo(node, service, hints, res);

    if ((hints->ai_family == AF_INET || hints->ai_family == AF_UNSPEC) &&
#ifdef WIN32
        inet_addr(node) != INADDR_NONE
#else
        inet_aton(node, &dummy_addr) != 0
#endif
        )
        return getaddrinfo(node, service, hints, res);
#ifdef AF_INET6
    if ((hints->ai_family == AF_INET6 || hints->ai_family == AF_UNSPEC) &&
        strchr(node, ':'))
        return getaddrinfo(node, service, hints, res);
#endif

    if ((hints->ai_flags & AI_NUMERICSERV) || *service == '0' ||
        atoi(service) > 0)
        return getaddrinfo(node, service, hints, res);

    if (hints->ai_socktype != SOCK_STREAM && hints->ai_socktype != SOCK_DGRAM)
        return getaddrinfo(node, service, hints, res);

    srv_name_size = 1 + strlen(service) + 2 + 3 + 1 + strlen(node) + 1;
    srv_name = (char *)malloc(srv_name_size);
    if (!srv_name)
        return EAI_MEMORY;

    snprintf(srv_name, srv_name_size, "_%s._%s.%s", service,
             hints->ai_socktype == SOCK_STREAM ? "tcp" : "udp", node);

#ifdef WIN32
    rc = DnsQuery(srv_name, DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, &resp, NULL);
#else
    rlen = res_search(srv_name, ns_c_in, ns_t_srv, resp, sizeof(resp));
#endif
    free(srv_name);

#ifdef WIN32
    if (rc != ERROR_SUCCESS)
#else
    if (rlen < 1)
#endif
        return getaddrinfo(node, service, hints, res);

#ifndef WIN32
    if (ns_initparse(resp, rlen, &nsb) < 0)
        return getaddrinfo(node, service, hints, res);
#endif

#ifdef WIN32
    for (entry = resp; entry != NULL; entry = entry->pNext)
    {
        switch (entry->wType)
        {
        case DNS_TYPE_SRV:
            snprintf(tgt_port, sizeof(tgt_port), "%d", entry->Data.SRV.wPort);
            tgt_name = entry->Data.SRV.pNameTarget;
#else
    for (i = 0; i < ns_msg_count(nsb, ns_s_an); i++)
    {
        rc = ns_parserr(&nsb, ns_s_an, i, &rrb);
        if (rc < 0)
            continue;

        if (ns_rr_class(rrb) != ns_c_in)
            continue;

        switch (ns_rr_type(rrb))
        {
        case ns_t_srv:
            rrlen = ns_rr_rdlen(rrb);
            if (rrlen < 8)
                break;

            p = ns_rr_rdata(rrb);
            rc = dn_expand(resp, resp + rlen, p + 6, tgt_name, sizeof(tgt_name));
            if (rc < 2)
                break;
            snprintf(tgt_port, sizeof(tgt_port), "%u",
                     (unsigned int)p[4] << 8 | (unsigned int)p[5]);
#endif
            if (getaddrinfo(tgt_name, tgt_port, hints, ai_last) != 0)
                break;

            for (ai = *ai_last; ai != NULL; ai = ai->ai_next)
                ai_last = &(ai->ai_next);

            break;
        default:
            break;
        }
    }

#ifdef WIN32
    DnsRecordListFree(resp, DnsFreeRecordList);
#endif

    if (ai_last == res)
        return getaddrinfo(node, service, hints, res);

    return 0;
}

/* ---------------------------------------------------------------------- */
/* 3. snprintf / vsnprintf fallback                                       */
/* ---------------------------------------------------------------------- */

#if !defined(HAVE_SNPRINTF) || !defined(HAVE_VSNPRINTF)

#define HAVE_VARARGS_H

#if defined(HAVE_STDARG_H)
# include <stdarg.h>
# define HAVE_STDARGS
# define VA_LOCAL_DECL   va_list ap
# define VA_START(f)     va_start(ap, f)
# define VA_SHIFT(v,t)   ;
# define VA_END          va_end(ap)
#else
# if defined(HAVE_VARARGS_H)
#  include <varargs.h>
#  undef HAVE_STDARGS
#  define VA_LOCAL_DECL   va_list ap
#  define VA_START(f)     va_start(ap)
#  define VA_SHIFT(v,t)   v = va_arg(ap,t)
#  define VA_END          va_end(ap)
# endif
#endif

#ifdef _MSC_VER
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef __int64* pint64_t;
#else
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef long long * pint64_t;
#endif

static void dopr (char *buffer, size_t maxlen, const char *format,
                  va_list args);
static void fmtstr (char *buffer, size_t *currlen, size_t maxlen,
                    char *value, int flags, int min, int max);
static void fmtint (char *buffer, size_t *currlen, size_t maxlen,
                    int64_t value, int base, int min, int max, int flags);
static void fmtfp (char *buffer, size_t *currlen, size_t maxlen,
                   long double fvalue, int min, int max, int flags);
static void dopr_outch (char *buffer, size_t *currlen, size_t maxlen, char c);

#define DP_S_DEFAULT 0
#define DP_S_FLAGS   1
#define DP_S_MIN     2
#define DP_S_DOT     3
#define DP_S_MAX     4
#define DP_S_MOD     5
#define DP_S_CONV    6
#define DP_S_DONE    7

#define DP_F_MINUS    (1 << 0)
#define DP_F_PLUS     (1 << 1)
#define DP_F_SPACE    (1 << 2)
#define DP_F_NUM      (1 << 3)
#define DP_F_ZERO     (1 << 4)
#define DP_F_UP       (1 << 5)
#define DP_F_UNSIGNED (1 << 6)

#define DP_C_SHORT   1
#define DP_C_LONG    2
#define DP_C_LDOUBLE 3

#define char_to_int(p) (p - '0')
#define MAX(p,q) ((p >= q) ? p : q)

static void dopr (char *buffer, size_t maxlen, const char *format, va_list args)
{
    char ch;
    int64_t value;
    long double fvalue;
    char *strvalue;
    int min;
    int max;
    int state;
    int flags;
    int cflags;
    size_t currlen;

    state   = DP_S_DEFAULT;
    currlen = flags = cflags = min = 0;
    max     = -1;
    ch      = *format++;

    while (state != DP_S_DONE)
    {
        if ((ch == '\0') || (currlen >= maxlen))
            state = DP_S_DONE;

        switch (state)
        {
        case DP_S_DEFAULT:
            if (ch == '%')
                state = DP_S_FLAGS;
            else
                dopr_outch(buffer, &currlen, maxlen, ch);
            ch = *format++;
            break;

        case DP_S_FLAGS:
            switch (ch)
            {
            case '-': flags |= DP_F_MINUS; ch = *format++; break;
            case '+': flags |= DP_F_PLUS;  ch = *format++; break;
            case ' ': flags |= DP_F_SPACE; ch = *format++; break;
            case '#': flags |= DP_F_NUM;   ch = *format++; break;
            case '0': flags |= DP_F_ZERO;  ch = *format++; break;
            default:  state = DP_S_MIN;    break;
            }
            break;

        case DP_S_MIN:
            if (isdigit((unsigned char)ch))
            {
                min = 10 * min + char_to_int(ch);
                ch  = *format++;
            }
            else if (ch == '*')
            {
                min = va_arg(args, int);
                ch  = *format++;
                state = DP_S_DOT;
            }
            else
                state = DP_S_DOT;
            break;

        case DP_S_DOT:
            if (ch == '.')
            {
                state = DP_S_MAX;
                ch    = *format++;
            }
            else
                state = DP_S_MOD;
            break;

        case DP_S_MAX:
            if (isdigit((unsigned char)ch))
            {
                if (max < 0)
                    max = 0;
                max = 10 * max + char_to_int(ch);
                ch  = *format++;
            }
            else if (ch == '*')
            {
                max = va_arg(args, int);
                ch  = *format++;
                state = DP_S_MOD;
            }
            else
                state = DP_S_MOD;
            break;

        case DP_S_MOD:
            switch (ch)
            {
            case 'h':
                cflags = DP_C_SHORT;
                ch = *format++;
                break;
            case 'l':
                cflags = DP_C_LONG;
                ch = *format++;
                if (ch == 'l')
                {
                    cflags = DP_C_LDOUBLE;
                    ch = *format++;
                }
                break;
            case 'L':
                cflags = DP_C_LDOUBLE;
                ch = *format++;
                break;
            case 'I':
                if (format[0] == '6' && format[1] == '4')
                {
                    cflags = DP_C_LDOUBLE;
                    format += 2;
                    ch = *format++;
                }
                break;
            default:
                break;
            }
            state = DP_S_CONV;
            break;

        case DP_S_CONV:
            switch (ch)
            {
            case 'd':
            case 'i':
                if (cflags == DP_C_SHORT)
                    value = (short int)va_arg(args, int);
                else if (cflags == DP_C_LONG)
                    value = va_arg(args, long int);
                else if (cflags == DP_C_LDOUBLE)
                    value = va_arg(args, int64_t);
                else
                    value = va_arg(args, int);
                fmtint(buffer, &currlen, maxlen, value, 10, min, max, flags);
                break;

            case 'o':
                flags |= DP_F_UNSIGNED;
                if (cflags == DP_C_SHORT)
                    value = (unsigned short int)va_arg(args, unsigned int);
                else if (cflags == DP_C_LONG)
                    value = va_arg(args, unsigned long int);
                else if (cflags == DP_C_LDOUBLE)
                    value = va_arg(args, uint64_t);
                else
                    value = va_arg(args, unsigned int);
                fmtint(buffer, &currlen, maxlen, value, 8, min, max, flags);
                break;

            case 'u':
                flags |= DP_F_UNSIGNED;
                if (cflags == DP_C_SHORT)
                    value = (unsigned short int)va_arg(args, unsigned int);
                else if (cflags == DP_C_LONG)
                    value = va_arg(args, unsigned long int);
                else if (cflags == DP_C_LDOUBLE)
                    value = va_arg(args, uint64_t);
                else
                    value = va_arg(args, unsigned int);
                fmtint(buffer, &currlen, maxlen, value, 10, min, max, flags);
                break;

            case 'X':
                flags |= DP_F_UP;
            case 'x':
                flags |= DP_F_UNSIGNED;
                if (cflags == DP_C_SHORT)
                    value = (unsigned short int)va_arg(args, unsigned int);
                else if (cflags == DP_C_LONG)
                    value = va_arg(args, unsigned long int);
                else if (cflags == DP_C_LDOUBLE)
                    value = va_arg(args, uint64_t);
                else
                    value = va_arg(args, unsigned int);
                fmtint(buffer, &currlen, maxlen, value, 16, min, max, flags);
                break;

            case 'f':
                if (cflags == DP_C_LDOUBLE)
                    fvalue = va_arg(args, long double);
                else
                    fvalue = va_arg(args, double);
                fmtfp(buffer, &currlen, maxlen, fvalue, min, max, flags);
                break;

            case 'E':
                flags |= DP_F_UP;
            case 'e':
                if (cflags == DP_C_LDOUBLE)
                    fvalue = va_arg(args, long double);
                else
                    fvalue = va_arg(args, double);
                break;

            case 'G':
                flags |= DP_F_UP;
            case 'g':
                if (cflags == DP_C_LDOUBLE)
                    fvalue = va_arg(args, long double);
                else
                    fvalue = va_arg(args, double);
                break;

            case 'c':
                dopr_outch(buffer, &currlen, maxlen, (char)va_arg(args, int));
                break;

            case 's':
                strvalue = va_arg(args, char *);
                if (max < 0)
                    max = (int)maxlen;
                fmtstr(buffer, &currlen, maxlen, strvalue, flags, min, max);
                break;

            case 'p':
                strvalue = (char *)va_arg(args, void *);
                fmtint(buffer, &currlen, maxlen, (long)strvalue, 16, min, max, flags);
                break;

            case 'n':
                if (cflags == DP_C_SHORT)
                {
                    short int *num = va_arg(args, short int *);
                    *num = (short int)currlen;
                }
                else if (cflags == DP_C_LONG)
                {
                    long int *num = va_arg(args, long int *);
                    *num = (long int)currlen;
                }
                else if (cflags == DP_C_LDOUBLE)
                {
                    pint64_t num = va_arg(args, pint64_t);
                    *num = (int64_t)currlen;
                }
                else
                {
                    int *num = va_arg(args, int *);
                    *num = (int)currlen;
                }
                break;

            case '%':
                dopr_outch(buffer, &currlen, maxlen, ch);
                break;

            case 'w':
                ch = *format++;
                break;

            default:
                break;
            }
            ch    = *format++;
            state = DP_S_DEFAULT;
            flags = cflags = min = 0;
            max   = -1;
            break;

        case DP_S_DONE:
            break;

        default:
            break;
        }
    }

    if (currlen < maxlen - 1)
        buffer[currlen] = '\0';
    else if (maxlen > 0)
        buffer[maxlen - 1] = '\0';
}

static void fmtstr (char *buffer, size_t *currlen, size_t maxlen,
                    char *value, int flags, int min, int max)
{
    int padlen, strln;
    int cnt = 0;

    if (value == 0)
        value = (char *)"<NULL>";

    for (strln = 0; value[strln]; ++strln)
        ;
    padlen = min - strln;
    if (padlen < 0)
        padlen = 0;
    if (flags & DP_F_MINUS)
        padlen = -padlen;

    while ((padlen > 0) && (cnt < max))
    {
        dopr_outch(buffer, currlen, maxlen, ' ');
        --padlen;
        ++cnt;
    }
    while (*value && (cnt < max))
    {
        dopr_outch(buffer, currlen, maxlen, *value++);
        ++cnt;
    }
    while ((padlen < 0) && (cnt < max))
    {
        dopr_outch(buffer, currlen, maxlen, ' ');
        ++padlen;
        ++cnt;
    }
}

static void fmtint (char *buffer, size_t *currlen, size_t maxlen,
                    int64_t value, int base, int min, int max, int flags)
{
    int signvalue = 0;
    uint64_t uvalue;
    char convert[20];
    int place = 0;
    int spadlen = 0;
    int zpadlen = 0;
    int caps = 0;

    if (max < 0)
        max = 0;

    uvalue = (uint64_t)value;

    if (!(flags & DP_F_UNSIGNED))
    {
        if (value < 0)
        {
            signvalue = '-';
            uvalue    = (uint64_t)(-value);
        }
        else if (flags & DP_F_PLUS)
            signvalue = '+';
        else if (flags & DP_F_SPACE)
            signvalue = ' ';
    }

    if (flags & DP_F_UP)
        caps = 1;

    do {
        convert[place++] =
            (caps ? "0123456789ABCDEF" : "0123456789abcdef")
            [uvalue % (unsigned)base];
        uvalue = (uvalue / (unsigned)base);
    } while (uvalue && (place < 20));
    if (place == 20)
        place--;
    convert[place] = 0;

    zpadlen = max - place;
    spadlen = min - MAX(max, place) - (signvalue ? 1 : 0);
    if (zpadlen < 0) zpadlen = 0;
    if (spadlen < 0) spadlen = 0;
    if (flags & DP_F_ZERO)
    {
        zpadlen = MAX(zpadlen, spadlen);
        spadlen = 0;
    }
    if (flags & DP_F_MINUS)
        spadlen = -spadlen;

    while (spadlen > 0)
    {
        dopr_outch(buffer, currlen, maxlen, ' ');
        --spadlen;
    }

    if (signvalue)
        dopr_outch(buffer, currlen, maxlen, (char)signvalue);

    while (zpadlen > 0)
    {
        dopr_outch(buffer, currlen, maxlen, '0');
        --zpadlen;
    }

    while (place > 0)
        dopr_outch(buffer, currlen, maxlen, convert[--place]);

    while (spadlen < 0)
    {
        dopr_outch(buffer, currlen, maxlen, ' ');
        ++spadlen;
    }
}

static long double abs_val (long double value)
{
    return (value < 0) ? -value : value;
}

static long double pow10_ld (int exp)
{
    long double result = 1;
    while (exp)
    {
        result *= 10;
        exp--;
    }
    return result;
}

static long round_ld (long double value)
{
    long intpart = (long)value;
    value -= intpart;
    if (value >= 0.5)
        intpart++;
    return intpart;
}

static void fmtfp (char *buffer, size_t *currlen, size_t maxlen,
                   long double fvalue, int min, int max, int flags)
{
    int signvalue = 0;
    long double ufvalue;
    char iconvert[20];
    char fconvert[20];
    int iplace = 0;
    int fplace = 0;
    int padlen = 0;
    int zpadlen = 0;
    int caps = 0;
    long intpart;
    long fracpart;

    if (max < 0)
        max = 6;

    ufvalue = abs_val(fvalue);

    if (fvalue < 0)
        signvalue = '-';
    else if (flags & DP_F_PLUS)
        signvalue = '+';
    else if (flags & DP_F_SPACE)
        signvalue = ' ';

    (void)caps; /* not used, but kept for symmetry */

    intpart = (long)ufvalue;

    if (max > 9)
        max = 9;

    fracpart = round_ld((pow10_ld(max)) * (ufvalue - intpart));

    if (fracpart >= pow10_ld(max))
    {
        intpart++;
        fracpart -= (long)pow10_ld(max);
    }

    do {
        iconvert[iplace++] =
            (caps ? "0123456789ABCDEF" : "0123456789abcdef")[intpart % 10];
        intpart = (intpart / 10);
    } while (intpart && (iplace < 20));
    if (iplace == 20)
        iplace--;
    iconvert[iplace] = 0;

    do {
        fconvert[fplace++] =
            (caps ? "0123456789ABCDEF" : "0123456789abcdef")[fracpart % 10];
        fracpart = (fracpart / 10);
    } while (fracpart && (fplace < 20));
    if (fplace == 20)
        fplace--;
    fconvert[fplace] = 0;

    padlen  = min - iplace - max - 1 - ((signvalue) ? 1 : 0);
    zpadlen = max - fplace;
    if (zpadlen < 0)
        zpadlen = 0;
    if (padlen < 0)
        padlen = 0;
    if (flags & DP_F_MINUS)
        padlen = -padlen;

    if ((flags & DP_F_ZERO) && (padlen > 0))
    {
        if (signvalue)
        {
            dopr_outch(buffer, currlen, maxlen, (char)signvalue);
            --padlen;
            signvalue = 0;
        }
        while (padlen > 0)
        {
            dopr_outch(buffer, currlen, maxlen, '0');
            --padlen;
        }
    }
    while (padlen > 0)
    {
        dopr_outch(buffer, currlen, maxlen, ' ');
        --padlen;
    }
    if (signvalue)
        dopr_outch(buffer, currlen, maxlen, (char)signvalue);

    while (iplace > 0)
        dopr_outch(buffer, currlen, maxlen, iconvert[--iplace]);

    dopr_outch(buffer, currlen, maxlen, '.');

    while (fplace > 0)
        dopr_outch(buffer, currlen, maxlen, fconvert[--fplace]);

    while (zpadlen > 0)
    {
        dopr_outch(buffer, currlen, maxlen, '0');
        --zpadlen;
    }

    while (padlen < 0)
    {
        dopr_outch(buffer, currlen, maxlen, ' ');
        ++padlen;
    }
}

static void dopr_outch (char *buffer, size_t *currlen, size_t maxlen, char c)
{
    if (*currlen < maxlen)
        buffer[(*currlen)++] = c;
}

#ifndef HAVE_VSNPRINTF
int vsnprintf (char *str, size_t count, const char *fmt, va_list args)
{
    if (count == 0)
        return 0;
    str[0] = 0;
    dopr(str, count, fmt, args);
    return (int)strlen(str);
}
#endif

#ifndef HAVE_SNPRINTF
#ifdef HAVE_STDARGS
int snprintf (char *str, size_t count, const char *fmt, ...)
#else
int snprintf (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    char *str;
    size_t count;
    char *fmt;
#endif
    VA_LOCAL_DECL;

    VA_START(fmt);
    VA_SHIFT(str,   char *);
    VA_SHIFT(count, size_t);
    VA_SHIFT(fmt,   char *);
    (void)vsnprintf(str, count, fmt, ap);
    VA_END;
    return (int)strlen(str);
}
#endif

#endif /* !HAVE_SNPRINTF || !HAVE_VSNPRINTF */
