# AmiBinkD

A native AmigaOS 3.x port of [binkd](https://github.com/pgul/binkd), the
FTN (FidoNet Technology Network) mailer, built without ixemul.library or
ixnet.library.

## Status: working on real Amiberry, both directions confirmed, soak-tested clean

Cross-compiles and links cleanly into a loadable AmigaOS binary
(`amibinkd`), zero references to ixemul/ixnet anywhere in the binary.
Tested live on the user's production Amiberry instance (deployed to
`DH0:AmiBinkd/`, per-network configs `cnet.cfg`/`araknet.cfg`/
`disciple.cfg`/`fidonet.cfg`/`pinet.cfg`/`retronet.cfg`/`amiganet.cfg`,
real FTN addresses/domains):

- **Outbound (client/poll)**: repeatedly connects to real remote binkds
  (`Mystic/1.12A49` and, on fidonet, the real reference `binkd/1.1a-115`
  itself), completes full BinkP handshake incl. CRAM-MD5 auth, exchanges
  SYS/LOC/ZYZ/TIME/VER/BUILD info, clean session close.
- **Inbound (server)**: accepts a real connection from the network hub,
  runs the full session (or a clean auth failure for a wrong per-node
  password - a config issue, not a bug), and correctly returns to
  waiting for the next connection afterward - the original hang this
  port fought hardest to fix.
- **Multi-node polling in one invocation** (`-P addr1 -P addr2 ...`, or
  `-P ALL` for every node in the config as of v10.15 - see below), and
  the equivalent pattern of polling each network via its own `Execute
  <net>.scr` in sequence from a driver script (`FTN_Poll`): confirmed
  working through all seven configured networks in one run, including
  **real inbound file reception and successful CNet/5 Toss import** of
  the received packets - not just clean-looking session logs.
- **Unattended soak test**: the fixed build (deployed 2026-07-19 23:32)
  ran unattended overnight, 30-minute polls across all seven networks,
  through 2026-07-20 morning with no gaps, no hangs, and no repeats of
  any of the bugs fixed this round - see "Soak test results" below.

Getting here took several rounds of real-hardware-only bugs - Amiberry's
`bsdsocket_emu` diverges from real Roadshow/real BSD sockets in more than
one place, this toolchain's `intmax_t`/`uintmax_t` size assumptions don't
match `sys.h`'s fallback format-string macros, and one bug (`o_rename()`)
was entirely our own - and none of these were visible from
cross-compiling alone. See "Real-hardware findings" below before touching
networking, logging, or filesystem code here.

## Background

The user's own `AmiBinkD-main` starting point (from GitHub) turned out to
be upstream binkd given a cosmetic "modernization" pass - reformatted,
several files quietly dropped without checking what depended on them
(`readcfg.c`/`.h`, defining the core `BINKD_CONFIG` struct, was missing
entirely), and `main.c`/`ftn.c` were stubs that never actually drove the
real `servmgr()`/`clientmgr()` logic already sitting in `server.c`/
`client.c`. Its one genuinely new, correct piece was `amiga_glue.c`
(opens `bsdsocket.library` directly - the right approach).

Traced the real ixemul dependency to upstream's own historical Amiga
port (`mkfls/amiga/Makefile`): "you have to install ADE and
**ixemul.library version 47.0 or newer**". Its `branch.c` (the
fork-equivalent used to run each binkp session) used ixemul's
`ix_vfork()`/`vfork_setup_child()`/`ix_vfork_resume()` directly - there
was no non-ixemul Amiga concurrency path in upstream at all.

Given that, this port starts from a clean upstream binkd checkout and
replaces every ixemul-era assumption with something native, rather than
patching the incomplete starting point piecemeal.

## Key architectural decision: real concurrency via `CreateNewProcTags` (v10.5+)

Classic AmigaOS has no `fork()`/`pthreads` without ixemul, but it does
have `CreateNewProcTags()` (dos.library) - the native primitive for
spawning a second running unit of execution. Classic AmigaOS is a single
flat shared address space across all tasks/processes (no fork()-style
copy-on-write isolation), so a process spawned this way behaves much
closer to a thread than to a Unix `fork()` - it shares the parent's
globals, heap, and open library bases directly.

Through v10.4, `branch.c`'s AMIGA case instead ran each binkp session
**synchronously**, in-process, taking the same fallback path upstream
binkd already has for platforms without `fork()`/threads (`DOS` - see
`Config.h`). That was an honest, working v1: only one binkp session
(inbound or outbound) could be active at a time. v10.5 replaced it with
a real `CreateNewProcTags(NP_Entry=...)` spawn per session, making
genuine concurrent sessions possible (e.g. an inbound connection arriving
while an outbound poll is already in flight, or multiple networks polled
back-to-back without waiting on each other).

Getting this right took more than just calling `CreateNewProcTags`:

- **Argument handoff with no built-in mechanism.** `NP_Entry` takes a
  bare function pointer - no `void*` parameter like `pthread_create()`.
  The child is spawned at `NP_Priority` one below the parent's current
  priority (`me->tc_Node.ln_Pri - 1`); AmigaOS's strictly
  priority-preemptive scheduler guarantees a ready lower-priority task
  never runs while a ready higher-priority task hasn't blocked, so the
  parent can safely write `newproc->pr_Task.tc_UserData` immediately
  after `CreateNewProcTags()` returns, with zero risk of the child
  observing it uninitialized - no semaphore/signal handshake needed. (An
  earlier design draft tried reusing `SIGF_SINGLE` as a signal-based
  handshake - wrong, since `SIGF_SINGLE` and `SIGF_BLIT`, graphics.
  library's blitter-arbitration signal, are the same physical bit per
  `exec/tasks.h`.)
- **`bsdsocket.library` per-Process access.** Roadshow's bsdsocket.library
  denies socket access to any Process other than the one that originally
  opened it, unless the opener calls
  `SocketBaseTagList(SBTC_CAN_SHARE_LIBRARY_BASES, TRUE, TAG_DONE)`
  (`amiga_glue.c`'s `amiga_socket_init()`) - without this, every session
  process but the first would silently fail to use sockets at all.
- **Shutdown ordering.** A `CreateNewProcTags(NP_Entry=...)` child has no
  `pr_SegList` of its own - only the parent's segment list references the
  code it's running. `exitfunc()`'s AMIGA drain loop therefore waits
  *indefinitely* (with periodic progress logging) for `n_servers`/
  `n_clients` to reach zero before actually exiting, rather than giving up
  after a few seconds the way the `HAVE_THREADS` case does - exiting out
  from under a still-running child risks it executing freed memory.
- **Existing thread-safety infrastructure**, already active for AMIGA but
  previously unexercised since `branch()` never spawned anything
  concurrent: `threadsafe()`, `MUTEXSEM`/`EVENTSEM`, `bsy_list`'s and
  `ftnnode.c`'s own dedicated semaphores, `config_sem`. A few genuinely
  unprotected spots were found and fixed alongside the new spawn path: the
  bare `--n_servers`/`--n_clients` decrements at session end (`server.c`/
  `client.c`), and `ftnq.c`'s `qn_free()` unconditionally clearing every
  node's in-memory `busy` flag on queue rebuild even for nodes whose
  sessions might still be running (now checks `bsy_test()` first).

**Accepted, documented limitations of this first concurrent release**:
`errno` (libnix's `__errno`) is a single unprotected global shared across
all concurrently-running Processes, and `mypid` (CRAM-MD5 nonce seed) and
`rand()`'s internal state are likewise shared/racy across concurrent
sessions - all judged low-severity and out of scope for this release. See
`manual.txt` section 05 for the full user-facing writeup.

## Toolchain

`~/tools/amiga-gcc-toolchain` (bebbo's modern m68k-amigaos-gcc 13.4.0b),
`-mcrt=nix13` - same convention as `cnet-re/RoFTIC_C`. Not ixemul.

```
export PATH="/home/spitfiretn/tools/amiga-gcc-toolchain/bin:$PATH"
make
```

## What had to change vs. plain upstream binkd, and why

- **`branch.c`**: AMIGA case now spawns each binkp session as a real
  AmigaOS Process via `CreateNewProcTags()` (v10.5+; ran synchronously,
  same path as `DOS`, through v10.4) instead of calling ixemul's
  `ix_vfork()`. See the architectural-decision section above for the
  argument-handoff, `bsdsocket.library`-sharing, and shutdown-ordering
  details this required.
- **`Config.h`**: its `#error "You must define HAVE_FORK or HAVE_THREADS"`
  now also accepts `AMIGA` as a valid third option.
- **`sem.h` / `amiga/sem.c`**: AMIGA had a `MUTEXSEM` type and
  `_InitSem`/`_LockSem`/etc. already, but no `EVENTSEM` type and no
  `_InitEventSem`/`_PostSem`/`_WaitSem`/`_CleanEventSem` (needed by e.g.
  the `SLEEP()` macro) - added both, as a mutex-protected flag polled via
  `Delay()`. The `extern MUTEXSEM hostsem; ...` block and its definitions
  in `binkd.c` were also gated on `HAVE_THREADS` only - extended to
  `HAVE_THREADS || AMIGA`.
- **`iphdr.h`**: new `#elif defined(AMIGA)` branch. `sock_init()`/
  `sock_deinit()` call `amiga_glue.c`'s `OpenLibrary`/`CloseLibrary`.
  `soclose()` uses `CloseSocket()`, not regular `close()` - bsdsocket
  handles aren't AmigaDOS file handles. Also defines `struct
  sockaddr_storage` as an alias for `sockaddr_in` - this toolchain's
  `netdb.h` has real `getaddrinfo()`/`getnameinfo()` (as bsdsocket.library
  inline-asm calls, verified working) but never defines
  `sockaddr_storage`, since there's no real IPv6 support to size it for.
- **`amiga_glue.c`/`.h`**: the one genuinely-new file from the starting
  point, kept and extended. Provides `amiga_socket_init()`/
  `_cleanup()`, plus thin wrappers for `select()` (→ `WaitSelect()`) and
  `inet_ntoa()` (→ `Inet_NtoA()`) - bsdsocket.library exports these under
  Amiga-specific names, not the plain BSD ones the rest of the codebase
  calls.
- **`run.c`**: `run()` (used by `srif.c`'s mail-event hooks, e.g.
  "run this program when a file arrives") now calls `SystemTagList()` -
  AmigaDOS's native command-line-interpreter call, the direct equivalent
  of `system()`. `run3()` (stdin/stdout/stderr-piped external commands -
  used by exactly one caller, an external-transport feature in
  `client.c`) is stubbed to fail cleanly: making bsdsocket sockets and
  AmigaDOS file handles interchangeable the way Unix fds are (which a
  real implementation needs) is its own project, not something to rush.
- **`binkd.c`**: `mypid` (used by the `PID()` macro and `md5b.c`'s
  challenge nonce) is now set once at startup via `FindTask(NULL)` - no
  real "pid" concept without fork. `chld()`/`sighandler()` (real
  SIGCHLD/waitpid machinery) stay `HAVE_FORK`-only; confirmed via
  `common.h`'s `check_child()` macro that they're never reachable
  without it.
- **`client.c`**: `n_clients` (gates `do_client()`'s "can I start another
  poll" check, and its "queue is empty, quitting" exit condition) was
  only ever decremented for `HAVE_THREADS` or `DOS`/`DEBUGCHILD` at the
  end of `call()` - never for `AMIGA`. Harmless for a single `-P` poll,
  but fatal for polling more than one node in one invocation: after the
  first session, the stale non-zero counter meant `do_client()` never
  correctly re-evaluated the queue, and the process just sat there after
  logging its own "session closed, quitting..." Fixed by adding `AMIGA`
  to the same `#elif` branch as `DOS`/`DEBUGCHILD`.
- **`amiga/rename.c`** (new file, not present in the AmiBinkD-main
  starting point): implements `o_rename()`, which `sys.h` maps `RENAME()`
  to for `AMIGA`/`UNIX` (upstream's own `if (!RENAME(...))`-based
  finalize/retry logic in `inbound.c` expects POSIX `rename()` semantics
  - 0 on success). Went through two rounds: it originally called
  AmigaDOS's `Rename()` correctly, but got the return-value sense
  backwards (`Rename()` returns *nonzero* on success, same convention as
  `Lock()`/`CreateDir()`/`DeleteFile()`, but the wrapper reported that as
  a POSIX failure and vice versa) and never set `errno` on the real
  failure path. See finding #8 below - this one was entirely our own bug,
  not an Amiberry divergence.
- **Compiler/libc quirks specific to this toolchain** (none ixemul-related,
  just this specific bebbo build's rough edges):
  - `sys.h` needs `-DHAVE_STDARG_H` or it falls back to pre-ANSI
    `va_alist`/`va_dcl` varargs syntax, which corrupts parsing of
    everything after it in the file.
  - Needs `-DHAVE_INTMAX_T` (toolchain's own `<stdint.h>` already
    provides `intmax_t`/`uintmax_t`) and `-DHAVE_SOCKLEN_T` (already
    provides `socklen_t`) to avoid conflicting redefinitions.
  - Any file mixing `sys.h` (pulls in `<unistd.h>`) with
    `<proto/bsdsocket.h>` (redefines `gethostname`/`inet_addr`/`select`/
    etc as inline-asm macros) **must** include `sys.h` first - otherwise
    the macros corrupt the plain prototypes processed afterwards. Hit
    this repeatedly; it's now a documented convention, not a one-off fix.
  - `srv_gai.c` (real DNS SRV-record lookup) needs `HAVE_RESOLV_H`,
    which this toolchain doesn't have - excluded from the build;
    `srv_gai.h`'s own fallback macro (`srv_getaddrinfo` → `getaddrinfo`)
    covers every caller.
  - `sys.h` falls back to defining `PRIuMAX`/`PRIdMAX` as `"lu"`/`"ld"`
    (4-byte `long`) whenever `<inttypes.h>` hasn't already defined them -
    which is always, in this build. But this toolchain's real
    `intmax_t`/`uintmax_t` (confirmed via `__UINTMAX_TYPE__`) is 8-byte
    `long long`, even on this 32-bit target. Every `Log()`/`msg_sendf()`
    call passing more than one `PRIuMAX`-formatted `boff_t`/`uintmax_t`
    argument in a single format string was corrupting its own variadic
    argument stream from that point on: each 8-byte value only got
    half-consumed by the 4-byte-wide `%lu`, shifting every later argument
    in the same call by 4 bytes. On this big-endian CPU that reads as the
    high 32 bits first - which is why a 7475-byte incoming file logged as
    `receiving foo.pkt (0 byte(s), off 7475)` instead of `(7475 byte(s),
    off 0)`, and, far more consequentially, corrupted the same fields in
    our *outgoing* `M_GET`/`M_FILE` protocol messages, which is what was
    actually causing "missing tmp file" failures on every session with
    real mail queued. Fixed by defining `PRIuMAX="llu"`/`PRIdMAX="lld"`
    for `AMIGA` specifically, ahead of the generic fallback.
  - This toolchain's `libgcc.a` is missing double-precision soft-float
    routines (`__adddf3`/`__muldf3`/etc) under every CPU/FPU flag
    combination tried - a genuine toolchain gap (the symbols exist as
    *text* in the archive but `ld`/`ar`/`nm` here were all built without
    LTO plugin support and can't read them; `~/tools/.../liblto_plugin.so`
    exists but isn't wired up). Rather than touch the shared toolchain
    (other projects depend on it) or hand-write a software IEEE754
    library, removed the only two real `double` usages in the whole
    codebase (both were purely cosmetic - a live transfer-percentage
    counter in `protocol.c`, and post-transfer CPS figures in
    `protocol.c`/`inbound.c`'s log lines) and rewrote them as integer/
    fixed-point math. Also **excluded `snprintf.c`** from the build -
    the toolchain's own `libnix` already provides real `snprintf`/
    `vsnprintf`, and upstream's fallback implementation needs `long
    double` (`pow10`/`round`) for `%f` support, which drags in the same
    broken `libgcc` dependency for code we don't even need.

## Real-hardware findings (Amiberry's bsdsocket_emu is not real Roadshow)

Every one of these compiled and linked fine, and looked correct from the
header/API surface alone - each was only caught by actually running on
real Amiberry. If something networking-related here seems to work in
cross-compilation but misbehaves live, check this list before assuming
the C code is at fault:

1. **`getaddrinfo()`/`getnameinfo()` aren't implemented**, despite
   `<inline/bsdsocket.h>` declaring them as real bsdsocket.library vector
   calls and this toolchain's `netdb.h` having the matching constants -
   calling them caused a wild-jump crash (Guru `8000000B`, Line-1111/
   unimplemented-instruction) on the very first call, before the config
   file was even opened. Fixed by forcing `rfc2553.h`'s AMIGA path to
   always use upstream's own self-contained emulation (`gethostbyname()`-
   based) instead of trusting the real vector - see `iphdr.h`'s
   `#undef getaddrinfo` etc. and `rfc2553.h`'s `&& !defined(AMIGA)`.
2. **`ioctl(FIONBIO)`/`fcntl(O_NONBLOCK)` don't work on socket
   descriptors** - they're AmigaDOS file-handle calls, and bsdsocket
   sockets aren't AmigaDOS file handles without ixemul's translation
   layer. Silently failed ("No such file or directory") every time,
   meaning sockets were never actually non-blocking. Fixed in
   `iptools.c`'s `setsockopts()`: AMIGA now uses `IoctlSocket()`,
   bsdsocket.library's real equivalent.
3. **A select()-ready listening socket's `accept()` is not guaranteed
   non-blocking**, unlike real BSD sockets. This was the original,
   hardest-to-find bug: server accepted a connection fine, but `accept()`
   then blocked forever right after `select()` had just reported that
   exact socket as ready - no crash, no error, just silence. Every other
   socket in the codebase gets set non-blocking via `setsockopts()`
   (`protocol.c`, on the *accepted* session socket) but the *listening*
   socket itself never did on any platform, since it was never needed
   before. Fixed in `server.c`'s `do_server()`: explicitly `setsockopts()`
   the listening socket too, and treat `EWOULDBLOCK`/`EAGAIN` from
   `accept()` as a normal retry instead of a fatal error.
4. **`WaitSelect()`'s `signals` parameter (for waking on Exec signals
   like `SIGBREAKF_CTRL_C`, not just socket activity) doesn't work** -
   passing it made no difference; Ctrl-C/`Break` still couldn't interrupt
   a blocked `select()` call, hanging a stuck process past even a
   `Break <pid> C` (needed a full reboot to clear).
5. **Calling `WaitSelect()` too rapidly breaks it** - a first fix attempt
   for #4 polled in a tight 0.25s loop instead, checking `SetSignal()` for
   CTRL_C between calls. That surfaced yet another bug: after a handful of
   rapid successive calls, `WaitSelect()` started returning `-1` with
   `errno` left at 0 ("No error"), no real error condition. Fixed in
   `amiga_glue.c`'s `select()`: when the caller already provides a
   bounded timeout (true almost everywhere in this codebase), make
   exactly **one** `WaitSelect()` call - matching original/vanilla call
   frequency - then check CTRL_C once after, via `SetSignal(0, mask)`
   (not `CheckSignal()` - this toolchain's `<proto/dos.h>` resolves to an
   older `ndk13-include` variant that doesn't declare it). Only polls (in
   1s steps) for the rarer case of a caller wanting to block indefinitely.
6. **Reverse DNS (`backresolv`) on a private/non-routable IP can hang
   the session** - not an Amiberry bug exactly, but a real trap: with
   `backresolv` enabled, an inbound connection from a LAN address (e.g.
   `192.168.x.x`) triggers a synchronous reverse-lookup that can never
   resolve, blocking that session until the *remote* side's own protocol
   timeout gives up first. Through v10.4's synchronous-only concurrency
   model this blocked everything else too; since v10.5's real concurrency
   it only blocks the one affected session process, but it's still a
   session that never completes on its own. `backresolv` is purely
   cosmetic (nicer hostnames in the log) - fine to leave disabled.
7. **`n_clients` silently never decremented for AMIGA** (`client.c`'s
   `call()`) - same class of bug as `n_servers` in `serv()` (`server.c`),
   but not harmless like that one: `n_servers`'s only consumer
   (`check_child()`) is a no-op without `HAVE_FORK`, while `n_clients`
   directly gates `do_client()`'s decision to poll the next queued node.
   A single `-P` poll never surfaced it (nothing left to decrement for);
   only testing multiple `-P` addresses in one invocation - or the
   equivalent, polling several networks back-to-back via separate
   `Execute <net>.scr` invocations from a driver script - exposed it as a
   hang right after the first session's own "session closed, quitting..."
   line. See the `client.c` bullet above for the fix.
8. **`o_rename()` (`amiga/rename.c`) had success/failure backwards, and
   never set `errno`** - unlike the other findings here, this was a bug
   in code we wrote for this port, not an Amiberry/bsdsocket_emu
   divergence, but it was just as invisible until live testing: AmigaDOS's
   `Rename()` returns *nonzero* on success (the standard AmigaDOS BOOL
   convention, same as `Lock()`/`CreateDir()`/`DeleteFile()`), but the
   wrapper reported that as a POSIX failure (`-1`) and a real AmigaDOS
   failure as POSIX success (`0`) - inverted - and set no `errno` on the
   failure path either. `inbound.c`'s finalize-the-received-file logic
   checks `errno` (`EEXIST`/`EACCES`/`EAGAIN`) to decide whether to retry
   under an alternate filename or give up; with both bugs stacked, a
   rename that had *already succeeded on disk* got reported as failed,
   with whatever stale `errno` happened to be sitting around (often 0,
   logging the actively misleading `cannot rename foo.pkt to it's
   realname: No error!`). This silently orphaned a real backlog of
   received-but-never-finalized packets under their temp names in
   `Inbound_Temp/` before it was caught - clearing on the next successful
   Toss run once the fix landed. Fixed by inverting the return mapping
   and translating `IoErr()` to a real `errno` via a small switch
   (`EEXIST`/`ENOENT`/`EBUSY`/`ENOSPC`/`EROFS`/`EXDEV`/`EINVAL`/`ENOMEM`,
   `EIO` fallback).
9. **UDP/datagram sockets don't reliably work at all** - discovered while
   chasing finding #11 below. A standalone test program doing nothing but
   `OpenLibrary("bsdsocket.library")` + `socket(AF_INET, SOCK_DGRAM, 0)`
   + one UDP send/receive hung indefinitely, twice, on two different
   instance ages (including immediately after a full guest reboot) -
   frozen before any actual network I/O, meaning the problem is in
   `bsdsocket_emu`'s own socket handling, not anything DNS/UDP-protocol
   specific. This project has no current use for UDP (BinkP is TCP-only)
   so it's undeveloped rather than fixed - noted here so nobody reaches
   for a UDP-based feature on this platform without testing it in
   isolation first.
10. **A blocking `bsdsocket.library` call's internal completion
    notification never reaches a spawned, non-opener Process** - the
    hardest bug in this whole project, and the real explanation behind
    what looked like several separate problems. Since v10.5, every
    outbound `connect()` (and, it turned out, every `gethostbyname()`
    call too) runs inside a `CreateNewProcTags`-spawned child
    (`branch.c`) that isn't the Process that originally opened
    `bsdsocket.library` (`amiga_glue.c`'s `amiga_socket_init()`, called
    once in the top-level process, with `SBTC_CAN_SHARE_LIBRARY_BASES`
    opted in so children can use that same shared instance at all - see
    the concurrency section above). That function's own comment already
    flagged the relevant documented tradeoff
    (`SBTC_SIGIOMASK`-style per-Process signal delivery only reaches
    "whichever Process last configured it") but reasoned only about
    `select()`, which this codebase already uses in bounded-polling mode,
    not signal-wait mode. It missed that a *blocking* library call's own
    internal completion notification likely depends on that exact same
    mechanism - so a spawned child's blocking `connect()`/
    `gethostbyname()` call never gets woken up, even though the real
    underlying operation (confirmed via a live `tcpdump` capture: TCP
    handshake and the remote's data both completing in well under a
    second) succeeds just fine at the kernel level the whole time. Two
    non-blocking-socket workarounds (`select()`-based, then a manual
    poll-loop) were both tried first and both failed identically live,
    which is what proved this wasn't about blocking-vs-non-blocking
    connect() at all. Fixed by giving each spawned child its own
    private, unshared `bsdsocket.library` instance for its whole session
    - redefining `BSDSOCKET_BASE_NAME` (the macro all ~40 of
    `bsdsocket.library`'s call-site stubs resolve through) to a function,
    `amiga_current_socketbase()`, that returns a Task's own private base
    (stashed in `tc_UserData`) if it opened one, or falls back to the
    existing shared base otherwise - so every other Process (the
    top-level opener, inbound session children, which were never
    affected since their I/O is already non-blocking+`select()`-based)
    sees zero behavior change. Confirmed live across all 7 configured
    networks, an overnight soak with zero failures, and (once the same
    diagnosis was applied to `gethostbyname()` too) a further two-day,
    two-reboot soak with ~60 clean poll cycles per network.

## Deployment gotcha: AmigaDOS script comments are `;`, not `#`

Not a code bug, but cost real debugging time so it's worth recording:
the driver scripts that poll each network in sequence (`cnet.scr`,
`pinet.scr`, etc., invoked in turn by `FTN_Poll`) are plain AmigaDOS
`Execute` scripts, which use `;` for comments - `#` is not a comment
character and `Execute` tries to run it as a literal (nonexistent)
command. Two of the seven per-network `.scr` files had `#`-prefixed
header comments (leftover from a Unix-shell-style template) that made
`Execute` fail immediately on the first line with `Unknown command` /
`failed returncode 10`, aborting not just that script but the whole
calling `FTN_Poll` chain - which looked identical to a code-level hang
until the exact stopping point was isolated by running each `.scr`
standalone.

## `-P ALL`: one config, one invocation, every network (v10.15)

Upstream binkd's `-P` takes one FTN address per switch, so this BBS drove
it the way the "Status" section above describes: one `.cfg` **and** one
`.scr` per network (`cnet.cfg` + `cnet.scr`, `fidonet.cfg` +
`fidonet.scr`, ...), invoked in sequence by `FTN_Poll`. Seven networks
meant fourteen files to keep in step, and every config-wide change (a new
`inbound` path, a `loglevel` bump, disabling `backresolv`) had to be made
seven times. FidoBlitz, the other Amiga binkp mailer on this system,
instead accepts `fidoblitz -p -PALL fidoblitz.cfg` - one config listing
every uplink, one command that polls all of them.

v10.15 adds the same thing here, so `AmiBinkd -p -PALL AmiBinkd:AmiBinkd.cfg`
polls every network in one run (`rofftn.scr`). `AmiBinkd.cfg` already
listed all seven domains/addresses/nodes because the **server** side
(`ChkAmiBinkDSvr` -> `AmiBinkd -s AmiBinkd.cfg`) has always used the
merged config; this makes the client side use the same one file.

Implementation (`poll_all_nodes()` in `ftnnode.c`, dispatched from
`binkd.c`'s poll-creation loop when the `-P` argument is `ALL`,
case-insensitively): iterate the config's node array with the existing
`foreach_node()` and `create_poll()` for each, i.e. the same
per-node work `poll_node()` already did, minus the address parsing.
`ALL` can't collide with a real `-P` argument because it isn't a valid
Fido-style address - the old code path would just have logged
"`ALL` cannot be parsed as a Fido-style address".

Two nodes are deliberately skipped rather than polled:

- **Nodes with no host to call** (`hosts` unset or `-`). `q_not_empty()`
  already refuses to call those, so creating polls for them would only
  litter the outbound with `.dlo` files nothing ever picks up. Logged at
  loglevel 4 and counted in the summary line.
- **Our own addresses** (anything in `config->pAddr`), in case one is
  also listed as a `node`.

Note the concurrency consequence, which is a config matter rather than a
code one: with every uplink pollable in a single run, the clientmgr will
spawn up to `maxclients` outbound sessions at once, and the default is
100 - i.e. all seven networks simultaneously, where the per-`.scr`
pattern was strictly one at a time. This BBS's `AmiBinkd.cfg` now sets
`maxclients 2` (what `fidoblitz.cfg` uses for the same node list) plus
`call-delay 5`, because `do_client()` sleeps `call_delay` - default 60s -
every time it finds `maxclients` sessions already running, which would
otherwise idle a full minute between each pair of polls. The per-network
`.cfg`/`.scr` files are untouched and still work if a single network ever
needs to be polled by itself.

## Not done yet

- No zlib/bzip2 compression (`WITH_ZLIB`/`WITH_BZLIB2` not defined -
  matches the historical Amiga build, which also shipped without them).
- No DNS SRV-record lookups (see `srv_gai.c` note above).
- `run3()`'s piped external-transport feature is stubbed (logs and
  returns failure) - not a core binkp/FTN feature, only one caller.
- Multi-day/production-scale soak testing - the single overnight run
  documented below (pre-v10.5, synchronous path) has since been followed
  by two more, covering the concurrency path itself: an overnight soak
  right after finding #10's real fix landed (14/14 clean cycles/network,
  zero failures), and a two-day, two-reboot soak validating the same fix
  also covers `gethostbyname()` (~60 clean cycles/network total, one
  non-reproducing blip on the first reboot only). Still not weeks of
  real production volume, but no longer just one night either.
- `errno`'s exposure at every `TCPERR()`/`TCPERRNO` call site besides the
  `getpeername()`/`getsockname()` critical section fixed for real
  concurrent load (`client.c`'s outbound connect/socket/bind, `server.c`'s
  servmgr setup, `iptools.c`'s ioctl paths) - same class of shared-global
  race as finding #10 above, but for `errno` specifically rather than
  bsdsocket.library's internal notification mechanism. Unconfirmed as an
  active problem at these other sites, not yet fixed.
- `backresolv` (reverse/PTR lookups) is disabled in this BBS's deployed
  configs as a precaution, even though finding #10's fix likely also
  covers it (same call class, same spawned-child pattern) - just hasn't
  actually been re-tested live the way outbound `gethostbyname()` was.
- The `.csy`/`.bsy` busy-flag file routinely fails to unlink
  (`error unlinking '...csy': Text file busy`) - the overnight soak
  test confirms this fires on essentially *every* session, one
  network-specific file per network (e.g. `39.902.0.0.csy` for
  amiganet, `64.500.0.0.csy` for cnet), not an intermittent glitch.
  Purely cosmetic: every affected session still logged `done (...,
  OK, ...)` right after. Still not root-caused - worth a look if it
  ever *does* coincide with a real failure, but not urgent.
- Same soak test also logged a handful (3 total, ArakNet x1, C=Net x2)
  of `error unlinking '...Inbound_Temp/*.dt': No such file or
  directory` immediately after a successful `done OK` session close -
  a session-cleanup step trying to remove a temp marker that was
  already gone. Harmless (the session it followed had already
  completed successfully both before and after), rare, not
  investigated further.
- Per-node password mismatches surfaced during testing are a config
  issue (`AmiBinkd.cfg`'s `node` lines vs. what the hub expects), not
  a code bug - worth double-checking all configured nodes, not
  something this port needs to fix.
- One tossed fidonet packet (`5d79da03.pkt`) logged `7 msg(s) (0
  dupe(s)/7 bad)` by CNet's own Toss - not yet investigated; likely
  pre-existing malformed mail rather than anything in this port, but
  worth a look if it recurs on fresh packets.

## Soak test results (2026-07-19 evening -> 2026-07-20 morning)

Fixed build deployed to `DH0:AmiBinkd/AmiBinkd` 2026-07-19 23:32, then
left running unattended overnight under the normal `FTN_Poll` /
30-minute-interval driver-script schedule across all seven networks.
Reviewed `DH3:CNet/SysData/log/*.log` the next morning:

- All seven per-network logs current through ~05:30-05:40 the next
  morning, 30-minute cadence, no gaps and no hangs.
- Every occurrence of the `o_rename()` `cannot rename ... No error!` bug
  (7 total, across ArakNet/FidoNet/PiNet/RetroNet) timestamps to
  *before* 23:32 - i.e. the pre-fix build. Zero recurrences since the
  fixed binary went live.
- Every occurrence of `disciple.scr`'s old `-P12:136/0` typo similarly
  predates 23:32; every disciple poll since used the corrected
  `-P12:316/0` and completed `done (..., OK, ...)`.
- Disk usage flat and unremarkable (22% used, 171G free) - no leaked
  temp files piling up.
- The two still-open cosmetic issues above (`.csy`/`.bsy` unlink and
  the rare `.dt` unlink) are the only things that showed up; neither
  ever blocked a session from completing.

Net result: every bug fixed this round held overnight under real
unattended production traffic. The only open item is the pre-existing,
purely cosmetic `.csy`/`.bsy` unlink issue.
