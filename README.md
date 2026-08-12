# AmiBinkD

A native AmigaOS 3.x port of [binkd](https://github.com/pgul/binkd), the
FTN (FidoNet Technology Network) mailer, built without ixemul.library or
ixnet.library.

---

## Current release: v10.18 — recommended

**v10.18 is the release to run.** Inbound and outbound both work, and the
session-hang arc that made v10.16 and v10.17 unsafe is closed. Verified over
a 19h37m unattended soak on the live BBS: 22 poll cycles (22 BEGIN /
22 END), 29/29 scheduled events finished, 66 sessions all `OK`, 82 packets
received, both queues drained, **zero errors and zero warnings**.

If you are on an earlier build, upgrade — with one config change:

    set-file-dates      leave OFF (the default on this port)

That keyword is new in v10.18 and is what closes the last hang. See
"The session-hang arc" below for why, and `manual.txt` section 06 for the
full keyword reference.

**There is no release archive in this repo, deliberately.** The `.lha` is
built on the Amiga side with `DH0:C/lha`, because the host tools available
here (lhasa, 7z) can only *read* LZH and not create it, and because
building it there preserves the AmigaOS protection bits and `.info` icon an
Aminet package carries. A stale archive named as the release is worse than
none — anyone cloning would reasonably assume it was current — so the
v10.0-era one was removed rather than left to rot. Build from source with
the toolchain below, or get the packaged release from the BBS.

### The version jump: v10.16 and v10.17 were never released

**Nothing shipped between v10.15 and v10.18.** Both intermediate versions
exist as builds and commits in this tree, and both are documented here and
in `manual.txt`, because the AmigaOS problems they uncovered are worth
passing on to anyone else porting to this platform. Neither is safe to run:

- **v10.16** — first build where inbound genuinely ran, but a hung session
  permanently leaked an AmigaOS Process, a socket and a bsdsocket.library
  instance. 107 of 128 sessions leaked in twelve hours; the leaked `.bsy`
  locks then made the BBS's own poll script abort before it launched the
  mailer, killing outbound mail for eleven hours with `error: 8` as the only
  symptom. In practice that was worse than v10.15's quieter failure, which
  is why the brief upgrade recommendation for it was withdrawn.
- **v10.17** — fixed the semaphore strand behind most of that (105 leaks →
  13), but sessions could still hang one at a time inside `touch()`. One such
  hang inside a poll client blocked CNet's event scheduler for 17 hours.
- **v10.15** — the previous public release, and inbound has been silently
  broken in it since v10.5. It fails safe: the server stops accepting after
  the first hang, so nothing accumulates and outbound keeps working. Upgrade
  from it to v10.18.

Separately, and worth doing whatever mailer you run: if your BBS event script
deletes `.bsy`/`.csy` files before polling, make sure a failed delete cannot
abort the script. On AmigaDOS that means wrapping those deletes in
`FAILAT 21` / `FAILAT 10`. A lock file held by a live session is enough to
kill the whole event otherwise, and `kill-old-bsy` already sweeps stale
locks for you.

---

## Status: inbound and outbound both confirmed working in production (v10.18)

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
  port fought hardest to fix. **This first ran at all in v10.16 and only
  became safe in v10.18**: through v10.15 every inbound session silently
  transmitted nothing and hung, taking the listener with it; in v10.16 and
  v10.17 sessions ran but leaked. Two dedicated sections below cover this -
  they are the single most important thing to understand before touching
  the AmigaOS socket glue or anything that touches a file the mailer holds.
- **Multi-node polling in one invocation** (`-P addr1 -P addr2 ...`, or
  `-P ALL` for every node in the config as of v10.15 - see below), and
  the equivalent pattern of polling each network via its own `Execute
  <net>.scr` in sequence from a driver script (`FTN_Poll`): confirmed
  working through all seven configured networks in one run, including
  **real inbound file reception and successful CNet/5 Toss import** of
  the received packets - not just clean-looking session logs.
- **Unattended soak tests**: several, each one gating a release. The
  latest (v10.18, 2026-08-10 → 08-11, 19h37m) ran 22 poll cycles and 66
  sessions with zero errors and zero warnings - see "Soak test results"
  below for that run and the earlier ones it supersedes.

Getting here took several rounds of real-hardware-only bugs - Amiberry's
`bsdsocket_emu` diverges from real Roadshow/real BSD sockets in more than
one place, this toolchain's `intmax_t`/`uintmax_t` size assumptions don't
match `sys.h`'s fallback format-string macros, AmigaOS's own
`SetFileDate()`/`utime()`/`unlink()` block forever rather than failing when
another Process holds the object, and two bugs (`o_rename()`, and a
Makefile with no header dependencies) were entirely our own - and none of
these were visible from cross-compiling alone. See "Real-hardware findings"
below before touching networking, logging, or filesystem code here.

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
- **`amiga/touch.c`, `amiga/delete.c`** (new files, same pattern as
  `rename.c` above): native `dos.library` replacements for libc calls that
  **block indefinitely on this port instead of failing**. `utime()` and
  `unlink()` each cost a live production stall before being replaced -
  `unlink()` was caught 2026-08-02 hanging an outbound poll for 40+ minutes
  immediately after `found old .csy file`, with none of `sdelete()`'s own
  bounded-retry error messages ever appearing, which is what proved the
  task was stuck *inside* `unlink()` rather than looping around it.
  `DeleteFile()` and `SetFileDate()` are single bounded calls that return a
  real error for a locked file, which is exactly what the existing retry
  logic was written to handle. The rule this port now follows: on AmigaOS,
  prefer a bounded `dos.library` call that returns an error over any libc
  wrapper that might wait on a lock. (`SetFileDate()` turned out to block
  too - see "The session-hang arc" below.)
- **`amiga/getfree.c`** (new file): `getfree()` via `Lock()` + `Info()`,
  AmigaDOS's native free-space query, rather than the `statfs`/`statvfs`
  variants upstream selects between - none of which this toolchain has.
- **`amiga/msleep.c`** (new file): sub-second sleep, because `tools.c`'s
  `Log()` retries opening the log file ten times **with no delay between
  attempts**. Ten `fopen()` calls take microseconds, so a competing task
  holding the file for a few milliseconds makes all ten fail and the
  message is discarded silently. Not cosmetic: on 2026-08-02 a dropped line
  made a session look like it hung inside `touch()` when it had not, and
  sent the investigation after a bug that was not there. Sysops lose log
  lines the same way.
- **`Makefile`**: `-MMD -MP` plus `-include $(OBJS:.o=.d)`, so headers are
  real prerequisites. Added in v10.18 after their absence produced a
  cleanly-linking binary that read `config->pDomains` from the wrong
  address - see the second half of "The session-hang arc".
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

## The inbound sessions never worked until v10.16 (and how the v10.12 fix half-missed it)

This is the most consequential bug the port has had, and it hid behind a
plausible-sounding assumption for four releases.

**Symptom.** Every inbound session logged `incoming from ...` and
`incoming session with ...`, then produced *nothing* — no error, no
timeout, no close. Meanwhile the remote gave up with `Session timeout` /
`Authorization failed`. Because a hung session never released the server
slot, the second connection after a restart was refused outright, so
inbound mail died within minutes of every start and only a restart
appeared to "fix" it. Outbound polling was flawless throughout, which is
exactly what made this so easy to misread as a network or peer problem.

**What the sockets showed.** The decisive measurement was on the host,
not in the log:

    ESTAB  Recv-Q 285  ...:24554 <- peer     bytes_received:285  (no bytes sent at all)

The peer's greeting had arrived and was never read, and *we had never put
a byte on the wire* — despite the log dutifully printing `send message
NUL SYS ...`. Those log lines record frames being queued into the output
buffer, not transmitted. Both halves of the session's I/O were dead, not
just the read side.

**Cause.** `servmgr` runs in the Process that opened
`bsdsocket.library` (binkd.c calls `servmgr()` directly), so `accept()`
is fine. Each session is then spawned as its own Process by `branch()` —
and that child went on using the *shared* `SocketBase` for a socket it
did not own.

v10.12 diagnosed precisely this ambiguity for outbound `connect()` and
fixed it with a private per-child library base, but explicitly exempted
inbound on the reasoning that the shared "select()-based I/O path ...
never depended on signal delivery in the first place". That was wrong:
`amiga_glue.c`'s `select()` is implemented on **`WaitSelect()`**, which
*is* a signal-wait. Its readiness signals go to whichever Process the
library last associated as "the caller" — never the child. So the child's
`select()` never reported the socket readable *or* writable, and the
session sat forever.

**Fix.** AmigaOS descriptors are meaningless across `SocketBase`s, so the
socket has to be *transferred* rather than shared — `bsdsocket.library`'s
documented mechanism for exactly this:

- `do_server()`, after `accept()`: `ReleaseSocket(fd, UNIQUE_ID)` detaches
  the socket and returns a transfer id. That id, plus the address family,
  is what `branch()` hands the child. If `branch()` fails the parent
  re-`ObtainSocket()`s it, so the socket is never orphaned inside the
  library with no owner left to close it.
- `serv()`, in the child: `amiga_open_private_socketbase()` first, then
  `ObtainSocket(id, family, SOCK_STREAM, 0)` to re-materialise the socket
  inside that private base. The base is closed *after* `soclose()`, since
  the socket lives in it.

Non-AMIGA builds keep the original path untouched under `#else`.

**A symptom that looked like a second bug.** While inbound was broken,
stalled sessions also never honoured `timeout` (`nettimeout`,
`DEF_TIMEOUT` = 5 min), lingering 46 minutes and longer. That needed no
separate fix: the timeout is driven by the same `select()` loop that was
dead. With `select()` working, a dead-air probe session now tears itself
down in ten seconds.

**Verified live** (2026-07-31, v10.16): five consecutive real inbound
sessions from the hub in 44 seconds, full handshake and CRAM-MD5 auth,
two packets received into `Inbound/`, clean `downing server...` teardown
each time, no leftover `.bsy` locks, listener still accepting afterwards.
Outbound unchanged — the same run's 5:00a poll pulled 13 files /
45,581 bytes from fidonet.

**Lesson for anyone extending this port:** any Process that is not the
one which opened `bsdsocket.library` must have its own library base, and
must obtain its sockets through `ReleaseSocket`/`ObtainSocket`. Sharing a
base across Processes via `SBTC_CAN_SHARE_LIBRARY_BASES` makes calls
*succeed* while silently breaking everything that depends on per-Process
signal delivery — which includes `WaitSelect()`, not merely blocking
`connect()`.

## The session-hang arc: v10.16 → v10.18

Making inbound *run* (v10.16, above) exposed what happens when a session
that runs can also hang. Two releases and two distinct bugs later it is
closed. Both bugs are the same shape, and it is the shape that matters more
than either instance: **a call that blocks forever instead of failing, made
while holding something other sessions need.**

### v10.17 — one blocked session stranded all the others

Inbound went dead daily. `n_servers` ratcheted upward until it crossed
`max_servers` (default 100) and every incoming session was refused with
`too many servers`. Outbound kept working throughout — separate process per
cycle — which is exactly what masked it.

Counting step markers across a 13-hour run pinned it:

    handoff: child base=      213    inbound children started
    handoff: child closing    108    -> 105 stranded
    pass 0 SELECT   in/out  236/235    select is fine
    pass 0 bsy_touch in/out 236/136    <- 100 stranded here

`bsy_touch()` held a single global semaphore across `touch()` and `Log()`
for every entry in `bsy_list`. One session blocking in that I/O pins the
semaphore, and every sibling then blocks forever in `ObtainSemaphore()` on
the next pass of its protocol main loop. It is self-amplifying: stranded
sessions keep their `.bsy` entries, so the list grows, so the lock is held
longer, so more sessions strand.

Two changes, either of which alone breaks the chain:

- **`TryLockSem()`** — non-blocking acquire via `AttemptSemaphore()`
  (`amiga/sem.c`, `sem.h`). `bsy_touch()` now skips its pass rather than
  ever blocking on the lock. Touching `.bsy` files is cosmetic upkeep and
  `BSY_TOUCH_DELAY` already tolerates being early or late.
- **No I/O under the lock.** `bsy_touch()` claims the pass, copies each
  `fa`/`bt` out under a brief lock, releases, then touches unlocked. Safe
  because cells are never freed (`bsy_get_free_cell()` reuses `FA_ZERO`'d
  ones) and `bsy_add()` only prepends, so it cannot splice into the part of
  the list still ahead of the walk.

Worth recording as a dead end: an earlier double-checked lock did not help.
Cutting the *number* of acquisitions does nothing about a lock *held across
blocking I/O*.

### v10.18 — the call that could never return

v10.17 measured 105 leaks → 13. Better, not fixed: a session could still
hang alone inside `touch()` itself. Counting markers across a 26-hour run
split the behaviour cleanly by time of day:

    inb_done() commit path
    day   (07:04-21:00): touch in 66 / out 63    fine
    night (21:00-09:06): touch in  7 / out  0    every one hung

The difference was the BBS's own overnight file-catalog jobs holding
`Inbound_Temp`. `touch()` is `SetFileDate()` on this port — written
precisely because the generic `utime()` blocked — but **it blocks too when
another Process holds the object.** It does not fail, it never returns,
AmigaOS gives it no timeout, and the session has no watchdog.

The cost was never just leaked sessions. One such hang inside a poll client
meant the driver script never finished, so CNet's event scheduler stayed
blocked for 17 hours: outbound mail dead, tossing stopped, packets piling
up in `Inbound/`, inbound sessions accumulating toward `max_servers`.

Fix: new config keyword **`set-file-dates`**, default **off on AmigaOS** and
on everywhere else so upstream behaviour is unchanged. Gated at
`bsy_touch()` (`bsy.c`), `inb_done()` (`inbound.c`) and the GET-violation
path (`protocol.c`), plus a backstop inside `amiga/touch.c` itself so
`srif.c` — and anything added later — is covered without anyone having to
remember. What it gives up is cosmetic: received files carry arrival time
rather than the sender's timestamp.

Measured live over 18h, spanning the overnight window that broke v10.17:

                              v10.16     v10.17      v10.18
    saturated                 13h        ~4 days     no
    children start/close      213/108    46/33       89/89
    n_servers peak            100 cap    14 -> 56    5
    inb_done touch in/out     66/63*     7/0*        351/0
    scheduler events                     hung 17h    59/59 finished

    * day/night split above; v10.18 never calls touch() at all.

### The build bug that fix introduced — read this before editing a header

More dangerous than the hang, and worth its own warning: adding
`set_file_dates` to the **middle** of `BINKD_CONFIG` shifted every later
member. The Makefile rule was a bare `%.o: %.c`, so editing `readcfg.h`
rebuilt only translation units whose `.c` had also changed — `ftnq.o`,
`ftnaddr.o`, `client.o` and `server.o` kept the old offsets.

The binary linked. It ran. It read `config->pDomains` from the wrong
address: every domain but the last came back `unknown domain`, and six of
seven networks silently stopped polling their hub. Nothing crashed; it just
quietly stopped calling out.

The struct change was legal — the *build* was not. Fixed with `-MMD -MP`
and `-include` of the generated `.d` files. Verified: `ftnq.o` now depends
on `readcfg.h`, and `unknown domain` went 36 → 0 after a clean rebuild.
**If you are working from an older checkout of this tree, `make clean`
before trusting a build.**

### Diagnostics

Every count above came from step markers through `banner()`, the protocol
main loop and `inbound.c`'s commit path. They are not in the shipped build
(about 800 KB of log per day), but they are preserved whole and
re-appliable:

    git apply    diag-instrumentation.patch     # re-add
    git apply -R diag-instrumentation.patch     # remove

Two cautions if you use them. **Read the counts filtered by timestamp,
never by line position** — the log has two writers with independent
offsets, and position-based analysis produced a confident false negative.
And if you have `nolog` masks in your config, comment out `handoff:*` and
`clientmgr*` first: masked, these greps return zero, which reads as "no
sessions" rather than "not logged".

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
(`AmiBinkd -s AmiBinkd.cfg`) has always used the merged config; this makes
the client side use the same one file.

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

## Making the log readable (v10.18)

A sysop could not tell where one session ended and the next began, and the
useful lines were buried. Three things fixed that, and only the first is
code:

1. **Five lines in `client.c`** — a blank line before each outbound session
   and before the final `END`, plus an explicit `END` marker. Stock binkd
   runs sessions together with nothing between them and finishes on `the
   queue is empty, quitting...`. The separation is the single biggest
   readability win and there is no way to get it from config.
2. **`loglevel` and `nolog`**, which binkd already ships — config, not
   code. `loglevel 4` is the useful setting: **level 3 is what emits the
   remote's identity block** (`SYS`/`ZYZ`/`LOC`/`NDL`/`VER`/`TIME`), so at
   level 2 sessions look anonymous. See `amibinkd-example.cfg` for a
   commented mask set.
3. **Five message strings shortened** for 80-column width, measured against
   a real poll: `BEGIN` 120 → 68 columns, `rcvd:` 89 → 71, `Polling` (was
   `creating a poll for`) 82 → 70, `Outgoing:`/`Incoming:` (was
   `outgoing/incoming session with`), `END` 84 → 66. Over-80 lines per poll
   went from 9 to 4.

That third item is a **deliberate divergence from stock binkd**, made for
line width and portability. If an upstream merge ever conflicts on wording,
this is why. An earlier attempt that renamed messages purely to match a
reference log was reverted — it diverged from every other binkd log in FTN
and broke anything that parses them, all for cosmetics obtainable from
config. Do not re-introduce message renames for looks.

**Two `nolog` traps that cost real time:**

- **One mask per WORD.** `nolog receiving *` adds `receiving` **and** `*`,
  and `*` matches every message — the log goes completely silent while the
  mailer runs normally. Masks must be single tokens; `*` spans spaces on
  its own, so `*->*Inbound*` still matches `x.pkt -> Inbound/x.pkt`.
- **Masks hide your own diagnostics.** `handoff:*` and `clientmgr*` cover
  the counters that found the whole v10.16-v10.18 leak. Comment them out
  before diagnosing a stall.

The mailer no longer identifies itself as part of any one BBS package. It
reports `VER AmiBinkd v10.18-binkp/1.1`, the same shape remotes use
(`Mystic/1.12A49 binkp/1.0`). Branding lives in the distribution's text
files only.

## Not done yet

- No zlib/bzip2 compression (`WITH_ZLIB`/`WITH_BZLIB2` not defined -
  matches the historical Amiga build, which also shipped without them).
- No DNS SRV-record lookups (see `srv_gai.c` note above).
- `run3()`'s piped external-transport feature is stubbed (logs and
  returns failure) - not a core binkp/FTN feature, only one caller.
- Multi-week production-scale soak testing. Every release since v10.5 has
  been gated on an unattended overnight run (see "Soak test results"
  below), and v10.18 has now held clean across a full night with zero
  errors - but that is nights, not weeks, and on one system.
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
- `conlog` is the one element of v10.18 not proven. It was disabled here on
  2026-08-01 on suspicion of freezing CNet's Control window when the poll
  runs under `Execute`, and re-enabled for the v10.18 soak, which was clean
  - but 19 hours is not proof. If your BBS front-end freezes during or
  after a poll, comment out `conlog` before investigating anything else.
- Per-node password mismatches surfaced during testing are a config
  issue (`AmiBinkd.cfg`'s `node` lines vs. what the hub expects), not
  a code bug - worth double-checking all configured nodes, not
  something this port needs to fix.
- One tossed fidonet packet (`5d79da03.pkt`) logged `7 msg(s) (0
  dupe(s)/7 bad)` by CNet's own Toss - not yet investigated; likely
  pre-existing malformed mail rather than anything in this port, but
  worth a look if it recurs on fresh packets.

## Soak test results

**v10.18 — 2026-08-10 → 2026-08-11, 19h37m.** The release soak, run
unattended under the normal 30-minute `FTN_Poll` schedule across all seven
networks:

- 22 poll cycles: **22 `BEGIN` / 22 `END`**, no unmatched starts.
- **29/29** scheduled events finished.
- **66 sessions, all `OK`**; 82 packets received; both queues drained.
- **Zero errors, zero warnings.**
- Zero `find_tmp_name: ... busy`, and zero orphaned partials left in
  `Inbound_Temp` (see `kill-old-partial-files` below).

Two long-standing cosmetic items from earlier releases are gone as of this
run and are no longer listed as open: the `.csy`/`.bsy` `error unlinking
'...': Text file busy` that used to fire on essentially every session, and
the rare `error unlinking '...Inbound_Temp/*.dt': No such file or
directory`. Both were addressed by the ETXTBSY retry and by `amiga/delete.c`
replacing `unlink()` with a bounded `DeleteFile()`.

One config change came out of this soak and is in the shipped example:
**`kill-old-partial-files 2h`**. Thirteen orphaned `.dt`/`.hr` pairs had
accumulated in `Inbound_Temp`, and every session logged `find_tmp_name: ...
Text file busy` scanning past them. Two hours is long enough for an
interrupted transfer to be resumed on the next poll; the 2013 Amiga port
used 30 seconds, which is aggressive enough to destroy a still-resumable
partial.

**Earlier soaks**, each gating its release: an overnight run after finding
#10's fix (14/14 clean cycles per network); a two-day, two-reboot run
confirming that fix also covers `gethostbyname()` (~60 clean cycles per
network, one non-reproducing blip on the first reboot); a 14.5h run for
v10.17 (0 `too many servers`, against v10.16 saturating in 13h); an 18h run
for v10.18 spanning the overnight window that broke v10.17 (89/89 children
start/close, 59/59 scheduler events). The original 2026-07-19 run predates
the concurrency path entirely and is superseded by all of the above.

## Credits

binkd is by Dima Maloff and its many contributors; this is a port of
upstream `binkd/1.1a-115`, not a rewrite. `COPYING` and `HISTORY` are
upstream's and unmodified.

**Rudi Timmermans (X-TReMe BBS)** wrote the earlier Amiga port, AmiBinkd
v5.00-v9.02 (2012-2013), built against ixemul.library. This port starts
from clean upstream rather than from his tree, but it carries his name for
the obvious reason — he did this first, on harder ground, and the log
format his v9.01 produced is close enough to this one that the two are
recognisably the same program a decade apart.
