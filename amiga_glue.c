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
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>

#include "amiga_glue.h"
#include "tools.h"
#include "common.h"

struct Library *SocketBase = NULL;

/* netdb.h declares "extern int h_errno;" (used by rfc2553.c's
 * gethostbyname()-based getaddrinfo/getnameinfo emulation for error
 * diagnostics) but nothing in this toolchain actually provides storage
 * for it - same class of gap as environ/environ_ptr. Not wired up to
 * bsdsocket.library's real per-call error state, so its value here is
 * cosmetic (diagnostic text only, not branched on for correctness) -
 * good enough to satisfy the link without pretending it's fully live. */
int h_errno = 0;

/* bsdsocket.library exports these under Amiga-specific names (WaitSelect,
 * Inet_NtoA - see inline/bsdsocket.h), not the plain BSD names the rest
 * of this codebase calls. Thin wrappers, no ixemul involved. */

/* Passing NULL for WaitSelect()'s signals mask (as this used to) tells
 * bsdsocket.library to wake ONLY for socket activity - CTRL_C (from the
 * CLI's own Break command, or a caller hitting Ctrl-C) never wakes it,
 * since nothing here is listening for it. Confirmed the hard way: a
 * process blocked in here couldn't be broken out of at all, needing a
 * full reboot to clear. Watch for SIGBREAKF_CTRL_C too, and when that's
 * what woke us (not real socket readiness), set binkd_exit (checked
 * throughout client.c/server.c/protocol.c/etc already) and fail the
 * call with EINTR - the same contract a real interrupted-by-signal
 * select() has on Unix, which the rest of this codebase already expects. */
__stdargs int select(int n, fd_set *readfds, fd_set *writefds,
                      fd_set *exceptfds, struct timeval *timeout)
{
    ULONG sigs = SIGBREAKF_CTRL_C;
    int rc = WaitSelect(n, (APTR)readfds, (APTR)writefds, (APTR)exceptfds,
                         (struct __timeval *)timeout, &sigs);

    if (sigs & SIGBREAKF_CTRL_C)
    {
        SetSignal(0L, SIGBREAKF_CTRL_C);
        binkd_exit = 1;
        errno = EINTR;
        return -1;
    }

    return rc;
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
