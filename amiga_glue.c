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
 * never wakes it. Two earlier fix attempts both failed on real Amiberry:
 *   1. Passing SIGBREAKF_CTRL_C in the signals mask - didn't work, likely
 *      unimplemented in bsdsocket_emu (an advanced/optional part of the
 *      spec), same story as getaddrinfo() turning out unimplemented.
 *   2. A tight 0.25s polling loop, checking CTRL_C between every
 *      WaitSelect() call - calling WaitSelect() far more often than
 *      vanilla code ever would triggered a NEW problem: it started
 *      returning -1 with errno left at 0 ("No error") after a handful of
 *      calls, on real Amiberry, with no real error condition. Looks like
 *      bsdsocket_emu doesn't tolerate being hammered with rapid repeat
 *      calls, independent of the signals-mask issue.
 *
 * Settled on the minimal-deviation approach: when the caller already
 * gives a bounded timeout (the common case everywhere in this codebase -
 * e.g. the server accept loop's rescan-delay-based wait), make exactly
 * ONE WaitSelect() call with that same timeout, matching vanilla
 * behaviour and call frequency, then check CTRL_C once after it returns
 * for whatever reason. CTRL_C responsiveness is then bounded by the
 * caller's own timeout (~1s here), not instant, but that's an acceptable
 * trade for not fighting an emulator quirk. Only fall back to polling
 * (in coarser 1s steps, to stay well clear of whatever triggered the
 * rapid-call bug above) for the rarer case of a caller wanting to block
 * indefinitely (timeout == NULL), since that's the one situation a
 * single bounded call can't cover at all. */
#define AMIGA_SELECT_POLL_USEC 1000000L /* 1s poll granularity (indefinite-wait case only) */

static int amiga_check_ctrl_c(void)
{
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
        return 1;
    }
    return 0;
}

__stdargs int select(int n, fd_set *readfds, fd_set *writefds,
                      fd_set *exceptfds, struct timeval *timeout)
{
    struct __timeval tv;
    int rc;

    if (timeout != NULL)
    {
        /* Bounded wait: single WaitSelect() call, same as vanilla code. */
        tv.tv_secs  = timeout->tv_sec;
        tv.tv_micro = timeout->tv_usec;

        rc = WaitSelect(n, (APTR)readfds, (APTR)writefds, (APTR)exceptfds, &tv, NULL);
        if (amiga_check_ctrl_c())
            return -1;
        return rc;
    }

    /* Indefinite wait: only case that actually needs polling. */
    {
        fd_set rin, win, ein;
        fd_set rtmp, wtmp, etmp;

        if (readfds)   rin = *readfds;
        if (writefds)  win = *writefds;
        if (exceptfds) ein = *exceptfds;

        for (;;)
        {
            if (readfds)   rtmp = rin;
            if (writefds)  wtmp = win;
            if (exceptfds) etmp = ein;

            tv.tv_secs  = 0;
            tv.tv_micro = AMIGA_SELECT_POLL_USEC;

            rc = WaitSelect(n, readfds ? (APTR)&rtmp : NULL,
                             writefds ? (APTR)&wtmp : NULL,
                             exceptfds ? (APTR)&etmp : NULL,
                             &tv, NULL);

            if (amiga_check_ctrl_c())
                return -1;

            if (rc != 0)
            {
                if (readfds)   *readfds = rtmp;
                if (writefds)  *writefds = wtmp;
                if (exceptfds) *exceptfds = etmp;
                return rc;
            }
            /* rc == 0: this poll's 1s slice timed out, nothing ready yet -
             * caller asked to block indefinitely, so keep going. */
        }
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
