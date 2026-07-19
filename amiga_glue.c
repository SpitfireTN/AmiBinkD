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

/* Passing NULL for WaitSelect()'s signals mask (as this originally did)
 * tells bsdsocket.library to wake ONLY for socket activity - CTRL_C
 * never wakes it. First fix attempt passed SIGBREAKF_CTRL_C in that
 * signals mask instead - still didn't work on real Amiberry (Break still
 * couldn't unstick a blocked process). Given Amiberry's bsdsocket_emu is
 * its own lightweight reimplementation (not real Roadshow) that already
 * turned out not to implement real getaddrinfo(), the signals parameter
 * (a fairly advanced/optional part of the WaitSelect spec) is a
 * plausible second thing it just doesn't honor.
 *
 * Don't depend on it at all: poll in short bursts with a real timeout
 * WaitSelect (fds->watch fully bsdsocket_emu's problem, but a bounded
 * wait definitely is - there's no advanced feature involved), and check
 * for CTRL_C ourselves between polls via CheckSignal() - a pure
 * Exec.library call with no bsdsocket_emu involvement whatsoever. */
#define AMIGA_SELECT_POLL_USEC 250000L /* 0.25s poll granularity */

__stdargs int select(int n, fd_set *readfds, fd_set *writefds,
                      fd_set *exceptfds, struct timeval *timeout)
{
    fd_set rin, win, ein;
    fd_set rtmp, wtmp, etmp;
    long remaining_us = -1; /* -1 == caller wants to block indefinitely */
    struct __timeval poll_tv;
    int rc;

    if (readfds)   rin = *readfds;
    if (writefds)  win = *writefds;
    if (exceptfds) ein = *exceptfds;

    if (timeout != NULL)
        remaining_us = (long)timeout->tv_sec * 1000000L + (long)timeout->tv_usec;

    for (;;)
    {
        if (readfds)   rtmp = rin;
        if (writefds)  wtmp = win;
        if (exceptfds) etmp = ein;

        poll_tv.tv_secs  = 0;
        poll_tv.tv_micro = AMIGA_SELECT_POLL_USEC;
        if (remaining_us >= 0 && remaining_us < AMIGA_SELECT_POLL_USEC)
            poll_tv.tv_micro = remaining_us;

        rc = WaitSelect(n, readfds ? (APTR)&rtmp : NULL,
                         writefds ? (APTR)&wtmp : NULL,
                         exceptfds ? (APTR)&etmp : NULL,
                         &poll_tv, NULL);

        if (rc != 0)
        {
            if (readfds)   *readfds = rtmp;
            if (writefds)  *writefds = wtmp;
            if (exceptfds) *exceptfds = etmp;
            return rc;
        }

        /* SetSignal(0, mask) atomically clears the given signal bits and
         * returns what they were beforehand - used here purely to check
         * (not set) CTRL_C, avoiding CheckSignal(): this toolchain's
         * <proto/dos.h> resolves to an older ndk13-include variant that
         * doesn't declare it at all, while <proto/exec.h>'s SetSignal()
         * (used for OpenLibrary/CloseLibrary already) is known-good. */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        {
            binkd_exit = 1;
            errno = EINTR;
            return -1;
        }

        if (remaining_us >= 0)
        {
            remaining_us -= AMIGA_SELECT_POLL_USEC;
            if (remaining_us <= 0)
            {
                /* Genuine timeout, nothing ready - same as a real
                 * timed-out select(): return 0 with empty fd sets. */
                if (readfds)   FD_ZERO(readfds);
                if (writefds)  FD_ZERO(writefds);
                if (exceptfds) FD_ZERO(exceptfds);
                return 0;
            }
        }
        /* remaining_us == -1: caller wants to block "forever" - keep polling */
    }
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
