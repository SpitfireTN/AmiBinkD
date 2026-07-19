/* amiga_glue.c - AmigaOS 3.x bsdsocket.library glue (no ixemul/ixnet) */

/* All "plain" system headers (sys.h, and anything declaring real BSD-named
 * prototypes like inet_ntoa/select) must come before proto/bsdsocket.h -
 * bsdsocket.library's headers redefine a bunch of those same names
 * (gethostname, inet_addr, select, ...) as function-like inline-asm
 * macros, which corrupts the plain prototypes if processed afterwards.
 * Same convention every other file here follows. */
#include "sys.h"
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <exec/types.h>
#include <exec/libraries.h>

#include <proto/exec.h>
#include <proto/bsdsocket.h>

#include "amiga_glue.h"
#include "tools.h"

struct Library *SocketBase = NULL;

/* bsdsocket.library exports these under Amiga-specific names (WaitSelect,
 * Inet_NtoA - see inline/bsdsocket.h), not the plain BSD names the rest
 * of this codebase calls. Thin wrappers, no ixemul involved. */

__stdargs int select(int n, fd_set *readfds, fd_set *writefds,
                      fd_set *exceptfds, struct timeval *timeout)
{
    return WaitSelect(n, (APTR)readfds, (APTR)writefds, (APTR)exceptfds,
                       (struct __timeval *)timeout, NULL);
}

__stdargs char *inet_ntoa(struct in_addr in)
{
    return (char *)Inet_NtoA(in.s_addr);
}

int amiga_socket_init(void)
{
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL)
    {
        Log(0, "Unable to open bsdsocket.library v4+ - is a TCP/IP stack running?");
        return -1;
    }

    return 0;
}

void amiga_socket_cleanup(void)
{
    if (SocketBase != NULL)
    {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}
