#ifndef RFC2553_SHIM_H
#define RFC2553_SHIM_H

/*
 * RFC2553 compatibility header for systems lacking:
 *   - getaddrinfo()
 *   - freeaddrinfo()
 *   - getnameinfo()
 *   - gai_strerror()
 *
 * This header is paired with the rewritten rfc2553.c implementation.
 * Fully C89‑compatible and Amiga‑safe.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* --------------------------------------------------------------
 * Error codes (negative values, matching POSIX semantics)
 * -------------------------------------------------------------- */
#ifndef EAI_NONAME
#define EAI_NONAME      -1
#define EAI_AGAIN       -2
#define EAI_FAIL        -3
#define EAI_NODATA      -4
#define EAI_FAMILY      -5
#define EAI_SOCKTYPE    -6
#define EAI_SERVICE     -7
#define EAI_ADDRFAMILY  -8
#define EAI_MEMORY      -9
#define EAI_SYSTEM      -10
#define EAI_UNKNOWN     -11
#endif

/* --------------------------------------------------------------
 * Flags for getaddrinfo() and getnameinfo()
 * -------------------------------------------------------------- */
#ifndef AI_PASSIVE
#define AI_PASSIVE      0x01
#endif

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST  0x01
#define NI_NUMERICSERV  0x02
#define NI_NAMEREQD     0x04
#define NI_DATAGRAM     0x08
#endif

/* --------------------------------------------------------------
 * addrinfo structure (RFC2553)
 * -------------------------------------------------------------- */
#ifndef HAVE_ADDRINFO
struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    size_t           ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};
#endif

/* --------------------------------------------------------------
 * Function prototypes
 * -------------------------------------------------------------- */
#ifndef HAVE_GETADDRINFO
int getaddrinfo(const char *nodename,
                const char *servname,
                const struct addrinfo *hints,
                struct addrinfo **res);
#endif

#ifndef HAVE_FREEADDRINFO
void freeaddrinfo(struct addrinfo *ai);
#endif

#ifndef HAVE_GAI_STRERROR
char *gai_strerror(int ecode);
#endif

#ifndef HAVE_GETNAMEINFO
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen,
                int flags);
#endif

#endif /* RFC2553_SHIM_H */
