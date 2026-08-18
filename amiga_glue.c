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
#include <exec/tasks.h>
#include <dos/dos.h>

/* This toolchain's ndk13-include search path (checked before the real
 * ndk-include - same gotcha already documented in branch.c and, for
 * proto/dos.h's CheckSignal, elsewhere in this file) shadows
 * <utility/tagitem.h> with a literal empty 1-byte stub file that
 * defines nothing, not even its own include guard - so every attempt
 * to #include it, direct or transitive (libraries/bsdsocket.h below
 * tries its own internal one), silently gets nothing. Declared here
 * directly, guarded by the real header's own guard macro name so nothing
 * double-defines if a fixed toolchain ever does provide it correctly. */
#ifndef UTILITY_TAGITEM_H
#define UTILITY_TAGITEM_H
typedef ULONG Tag;
struct TagItem
{
    Tag   ti_Tag;
    ULONG ti_Data;
};
#define TAG_DONE ((ULONG) 0)
#define TAG_USER ((ULONG) (1UL << 31))
#endif

/* v10.12: redirect every bsdsocket.library call site (this file's own
 * direct <libraries/bsdsocket.h>/<proto/bsdsocket.h> includes below,
 * which happen BEFORE this file reaches its own amiga_glue.h) to a
 * per-Task-aware lookup instead of the bare shared SocketBase global -
 * see amiga_current_socketbase()'s own comment further down for why.
 * Duplicated here and in amiga_glue.h (which covers every other file
 * that reaches bsdsocket declarations via iphdr.h) for the same reason
 * TAG_DONE/TAG_USER are duplicated between this file and branch.c: each
 * file's own include order needs the workaround before its first
 * bsdsocket include, not just the shared header's. */
extern struct Library *amiga_current_socketbase (void);
#define BSDSOCKET_BASE_NAME amiga_current_socketbase()

#include <libraries/bsdsocket.h>

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

/* v10.23: tell the CURRENT SocketBase where to write errno.
 *
 * Must be called once per SocketBase, by the Task that will use it -- the
 * setting lives in the library base, not globally. Called from
 * amiga_socket_init() for the shared base and from
 * amiga_open_private_socketbase() for each per-child private base.
 *
 * Without this, errno is never updated by socket calls and every TCPERR()
 * on this port reports a stale value from some earlier, unrelated
 * operation. See the fuller note in amiga_socket_init(). */
void amiga_set_errno_ptr(void)
{
    struct TagItem errTags[2];

    errTags[0].ti_Tag  = SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno)));
    errTags[0].ti_Data = (ULONG) &errno;
    errTags[1].ti_Tag  = TAG_DONE;
    SocketBaseTagList(errTags);
}

int amiga_socket_init(void)
{
    struct TagItem shareTags[2];

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL)
    {
        Log(0, "Unable to open bsdsocket.library v4+ - is a TCP/IP stack running?");
        return -1;
    }

    /* Roadshow's bsdsocket.library denies Processes other than the one
     * that opened it access to the library's functions by default (see
     * bsdsocket.doc's OpenLibrary entry) - since v10.5 runs each BinkP
     * session as its own real AmigaOS Process (branch.c), every session
     * but this one would otherwise get silently refused socket access.
     * SBTC_CAN_SHARE_LIBRARY_BASES opts back into the pre-v4 "any caller
     * may use this SocketBase" behaviour. The documented cost (error
     * reporting becomes per-SocketBase rather than per-Process, and
     * SBTC_SIGIOMASK-style per-Process signal delivery only works for
     * whichever Process last configured it) was originally judged to
     * cost nothing here, reasoning only about select() (already
     * bounded-polling, not signal-wait - see select()'s own comment
     * above). v10.12 found that reasoning incomplete: a *blocking*
     * connect() call's own internal completion notification also
     * depends on this same per-Process delivery, which is why every
     * outbound poll (client.c's connect(), spawned as a non-opener
     * child via branch()) hung indefinitely despite the real TCP
     * handshake completing in well under a second - confirmed live via
     * packet capture. See amiga_current_socketbase() below and
     * client.c's call() for the fix: outbound-connecting children now
     * get their own private, unshared bsdsocket.library instance
     * instead of relying on this shared one for anything beyond
     * inbound sessions and the shared select()-based I/O path, which
     * never depended on signal delivery in the first place. */
    shareTags[0].ti_Tag  = SBTM_SETVAL(SBTC_CAN_SHARE_LIBRARY_BASES);
    shareTags[0].ti_Data = TRUE;
    shareTags[1].ti_Tag  = TAG_DONE;
    SocketBaseTagList(shareTags);

    /* v10.23: point this SocketBase at our errno.
     *
     * bsdsocket.library does not touch a program's errno unless handed a
     * pointer to it. AmiBinkD never did this, yet iphdr.h defines
     * TCPERR()/TCPERRNO on AMIGA as strerror(errno)/errno - so every socket
     * error binkd reported was whatever unrelated call last set errno.
     *
     * That is not cosmetic. It produced the long-standing "recv: Permission
     * denied" lines, which are EACCES left over from a failed unlink() of a
     * .bsy lock (271 of those on 2026-08-17 alone) - the socket never had a
     * permission error at all. Worse, recv_block()/send_block() branch on
     * TCPERRNO == EWOULDBLOCK/EAGAIN to decide whether an error is
     * retryable, so a stale value could make a fatal error look retryable
     * or vice versa.
     *
     * Note the comment above: opting into SBTC_CAN_SHARE_LIBRARY_BASES
     * already made error reporting per-SocketBase rather than per-Process.
     * That makes setting this MORE important, not less - and it must be set
     * on every base, which is why amiga_open_private_socketbase() below
     * does the same for each private child base. */
    amiga_set_errno_ptr();

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

/* v10.12: every bsdsocket.library call site in this codebase (this
 * file's own, plus every other file that reaches bsdsocket declarations
 * via iphdr.h -> amiga_glue.h) resolves through BSDSOCKET_BASE_NAME,
 * redefined (see the #define near this file's own bsdsocket includes,
 * and amiga_glue.h) to call this function instead of reading the bare
 * shared SocketBase global directly. A Task that has installed its own
 * private base via amiga_open_private_socketbase() (tc_UserData) gets
 * it; every other Task - the top-level opener, inbound session children
 * that never call that function, anything spawned before this release -
 * falls straight through to the existing shared SocketBase, completely
 * unchanged from pre-v10.12 behaviour. branch.c's amiga_proc_trampoline()
 * resets tc_UserData to NULL for every freshly spawned child specifically
 * so this fallback is always safe by default. */
struct Library *amiga_current_socketbase(void)
{
    struct Library *priv = (struct Library *) FindTask(NULL)->tc_UserData;
    return priv != NULL ? priv : SocketBase;
}

/* Opens a private, unshared bsdsocket.library instance for the calling
 * Task and installs it via tc_UserData so amiga_current_socketbase()
 * (and therefore every bsdsocket call this Task makes from here on)
 * picks it up automatically. Used by client.c's call() for each spawned
 * outbound-poll child - see that file for why: a blocking connect()'s
 * internal completion notification only reaches whichever Process the
 * library associates as "the caller," which for a shared SocketBase is
 * whichever Process last configured it, not necessarily this one. An
 * unshared instance sidesteps that ambiguity entirely - this is
 * bsdsocket.library's own default, most-tested mode of use (see
 * bsdsocket.doc's OpenLibrary entry), not a fragile special case.
 * Returns NULL on failure (caller should log and fall back to the
 * shared SocketBase rather than aborting - matches this codebase's
 * general degrade-rather-than-abort posture elsewhere). */
struct Library *amiga_open_private_socketbase(void)
{
    struct Library *priv = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (priv != NULL)
    {
        FindTask(NULL)->tc_UserData = (APTR) priv;
        /* v10.23: the errno pointer is a per-SocketBase setting, so a fresh
         * private base starts out not reporting errno at all. Set it here,
         * AFTER tc_UserData is assigned, so amiga_current_socketbase() -- and
         * therefore the SocketBaseTagList() call inside -- resolves to this
         * new private base rather than the shared one. */
        amiga_set_errno_ptr();
    }
    return priv;
}

/* Closes a private base opened by amiga_open_private_socketbase() and
 * resets tc_UserData back to NULL (safe no-op if lib is NULL, e.g. the
 * open failed and the caller degraded to the shared SocketBase). Must
 * be called from the SAME Task that opened it. */
void amiga_close_private_socketbase(struct Library *lib)
{
    if (lib != NULL)
    {
        CloseLibrary(lib);
        FindTask(NULL)->tc_UserData = NULL;
    }
}
