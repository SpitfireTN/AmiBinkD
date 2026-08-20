Short:        Native AmigaOS FTN mailer (binkd port, no ixemul/ixnet)
Uploader:     spitfiretn@gmail.com
Author:       Gary "Spitfire" McCulloch (Amiga port); binkd by Dima Maloff
              and the binkd project
Type:         comm/fido
Version:      10.25
Architecture: m68k-amigaos
Distribution: Aminet
Kurz:         Nativer AmigaOS FTN-Mailer (binkd-Port, ohne ixemul/ixnet)
Requires:     bsdsocket.library (Roadshow/AmiTCP/Amiberry bsdsocket_emu),
              C-Net/5 Amiga Pro or another FTN-aware BBS engine

AMIBINKD
Native AmigaOS 3.x port of binkd, the FTN mailer
===============================================================================

DESCRIPTION
===============================================================================

AmiBinkD is a native AmigaOS 3.x build of binkd, the FidoNet Technology
Network (FTN) mailer, used to send and receive FTN echomail/netmail
packets over TCP/IP using the BinkP protocol.

Unlike every prior Amiga build of binkd, this one does not require
ixemul.library or ixnet.library. It talks to bsdsocket.library directly,
and every place the historical Amiga port depended on ixemul's vfork()-
based process model has been replaced with a native AmigaOS equivalent:
each BinkP session (inbound or outbound) runs as its own real AmigaOS
process, spawned via CreateNewProcTags() -- genuine concurrent sessions,
not one session at a time.

This is a from-scratch reconstruction, not a recompile of the old ixemul
Amiga port. It was built by tracing every ixemul/ixnet dependency in
upstream binkd's own historical Amiga support and replacing each one with
a direct AmigaOS/bsdsocket.library equivalent, then hardening the result
against several real behavioral differences between Amiberry's
bsdsocket_emu and genuine Roadshow/BSD sockets that only showed up under
live testing, not at compile time. See manual.txt section 15 for the
full list, if you're maintaining this on a different TCP/IP stack.

If you carry several FTN networks, one config file and one command line
cover all of them:

  AmiBinkD:AmiBinkD -p -PALL AmiBinkD:AmiBinkD.cfg

"-P ALL" polls every node listed in the config, instead of naming each
uplink with its own -P switch or keeping a separate config and script
per network. See manual.txt section 07.

For full installation notes, configuration reference, known real-hardware
findings, and version history, see manual.txt.


FILES INCLUDED
===============================================================================

AmiBinkD                  Compiled AmigaOS executable (m68k, AmigaOS 3.0+)
AmiBinkD.info             Workbench icon (colour image needs AmigaOS 3.5+)
readme.txt                Quick overview and install notes
manual.txt                Full SysOp documentation
amibinkd-example.cfg      Example configuration file
amibinkd.scr              Example AmigaDOS poll-all driver script

Copy amibinkd-example.cfg to a name of your choosing (e.g. AmiBinkD.cfg,
or one file per network -- see manual.txt section 07) and edit it for
your system before running.

amibinkd.scr is a ready-to-edit "-P ALL" poll script -- point it at your
config name and schedule it from your BBS's event system.


REQUIREMENTS
===============================================================================

* Commodore Amiga or compatible/emulated Amiga system, AmigaOS 3.0+ (m68k)
* bsdsocket.library -- Roadshow, AmiTCP, or (as tested) Amiberry's
  bsdsocket_emu
* An FTN-aware BBS engine or tosser to hand packets to/from (built and
  tested against C-Net/5 Amiga Pro, but AmiBinkD itself only reads/writes
  standard FTN packet and outbound-flow files -- it doesn't call into
  C-Net/5 directly)
* At least one FTN address and an uplink/hub willing to carry your mail

AmiBinkD is a standalone compiled executable. It has no ixemul.library or
ixnet.library dependency of any kind -- confirmed via string search on
the built binary.


CONCURRENCY
===============================================================================

Classic AmigaOS has no fork()/pthreads without ixemul, but it does have
CreateNewProcTags() (dos.library) -- the native primitive this port uses
to spawn each BinkP session as its own real process. An inbound
connection arriving while an outbound poll is already in flight, or
several networks polled back-to-back, no longer have to wait their turn.
See manual.txt section 05 for the full technical writeup of how this
works and what it took to get right on classic AmigaOS specifically.


QUICK INSTALL
===============================================================================

1. Copy the AmiBinkD folder to your BBS mail directory.

   Example:

   Mail:AmiBinkD/AmiBinkD

2. Copy amibinkd-example.cfg to AmiBinkD.cfg (or a per-network name --
   see manual.txt section 07) and edit it: your domain(s), your FTN
   address(es), sysname/location/sysop, log path, inbound directories,
   and your node/hub line(s) with the real host, port, and password.

3. Create the directories your config references if they don't already
   exist: the outbound directory named in each "domain" line, the
   inbound/temp-inbound directories, and the log directory.

4. Test an outbound poll manually first, from a Shell:

   AmiBinkD:AmiBinkD -p -P<address> AmiBinkD:AmiBinkD.cfg

   Once that works, "-P ALL" in place of "-P<address>" polls every node
   in the config in one go.

5. Review the log, then set up your regular polling schedule and/or
   inbound server (see manual.txt sections 09-10).


BASIC MAIL FLOW
===============================================================================

1. AmiBinkD polls (outbound) or accepts a connection (inbound), performs
   the BinkP handshake, and exchanges queued files/packets over TCP.
2. Received .pkt files land in your configured inbound directory.
3. Your tosser (e.g. C-Net/5's Toss) processes them into your message
   bases.
4. Outbound mail queued by your BBS/tosser into the domain's outbound
   directory gets picked up and sent on the next poll.


===============================================================================
VERSION HISTORY
===============================================================================

v10.25 - Log Lines Stop Being Shredded
--------------------------------------

* Log lines were being torn apart mid-write when two AmiBinkd instances ran
  at once. A real example:

      + 18 Aug 21:07:52 [1081963032] pwd protected session (MD5)
      nux/64                      <- fragment of "Linux/64"
      115                         <- fragment of "binkd/1.1a-115"

  Log() already took a lock, but a PRIVATE one. Upstream binkd is a single
  process with threads, so a private semaphore covers every writer it has.
  This port runs the inbound server and each poll as separate program
  invocations, so two or three instances write the same file with no lock
  between them.

  Log() now uses an Exec PUBLIC semaphore ("AmiBinkd.log"), which is the
  AmigaOS mechanism for locking across separate programs.

  NOTE: the fix only holds once EVERY instance is running v10.25. Replacing
  the binary is not enough on its own -- the long-running inbound server keeps
  the old code until AmigaOS restarts it, and a v10.24 server will still
  interleave with a v10.25 poll.

* Removed the "servmgr listen on *:24554" line. The port is already in the
  config, and the line cost width without adding anything.

* The startup banner now credits the port alongside the original author.


v10.24 - Outbound Mail Actually Leaves The System
-------------------------------------------------

* THE BIG ONE: no outbound mail had ever left this port. Every bundle went
  out at 0 bytes while a perfectly good ZIP sat on disk holding the packet.
  Nothing logged an error, because no layer had one -- the tosser packed
  correctly, and AmiBinkd sent exactly the number of bytes it was told the
  file contained.

  The cause is in the C library, not in binkd. libnix13's fstat() always
  reports st_size == 0: it seeks to the start of the file and then measures
  with SEEK_CUR, which can only ever return zero. It should use SEEK_END.
  fstat() SUCCEEDS while doing this, so every caller receives a valid-looking
  struct describing an empty file.

  protocol.c does fopen + fstat + use st_size, exactly as upstream binkd
  does on every other platform. amiga/fstat.c now overrides fstat() to
  re-measure with SEEK_END and restore the caller's file position.

  If you run any Amiga software built with -mcrt=nix13 that depends on file
  sizes, this bug affects it too.

* Socket errors now name their real cause. bsdsocket.library never wrote to
  errno because SBTC_ERRNOPTR was never set, yet binkd reads socket errors
  as strerror(errno) on AmigaOS -- so every socket error printed a stale
  value left by some unrelated call, usually "Permission denied" from a
  failed .bsy unlink. You will now see "Connection reset by peer" and
  "Broken pipe" where the log used to blame permissions.

* AmigaOS "Please insert volume ..." requesters no longer freeze the mailer.
  pr_WindowPtr is set to -1 so DOS errors return to the caller instead of
  waiting for a human to click Cancel.

* Poll times dropped from ~14s to 3-5s. Removing one of our own lock files
  used a 4-second blocking delete; it is now ten bounded retries totalling
  about 400ms, and stale-lock cleanup makes a single non-blocking attempt.

* libnix stdio's global file list is now guarded by a semaphore. Sessions
  are real concurrent AmigaOS Processes sharing one address space, and
  libnix's stdio has no locking of its own.


v10.18 - Inbound Sessions Stop Hanging, Readable Session Log
-----------------------------------------------------------

* The session log was rewritten to be readable on an 80-column screen --
  a blank line between sessions, an explicit END marker, and shorter
  messages. "BEGIN" alone went from 120 columns to 68; it had been the
  only line in a poll that wrapped twice. Lines over 80 per poll dropped
  from nine to four.

  Everything else is done from configuration, not code. "loglevel"
  controls detail. Level 4 is the default and is meant to be read as-is:
  one line per connection, one per file transferred, one per result, with
  the remote's SYS name and FTN addresses. Raise it to 5 or higher for the
  full protocol conversation -- the remote's greeting block, mode
  negotiation, per-file bookkeeping and socket handoff all live there.

  Because level 4 is already trimmed, the "nolog" masks that earlier
  versions needed are gone from the sample config. "nolog" is still
  supported and documented in manual section 06, for masking a specific
  message once you have raised loglevel. It takes ONE MASK PER WORD: a
  mask written with spaces is split, and a stray bare "*" among the
  pieces silences the log completely.

* AmiBinkD no longer identifies itself as part of any BBS package. The
  VER string sent to every node you poll is now "AmiBinkd v10.18-binkp/1.1",
  matching the form other mailers use. It previously named a specific BBS,
  which was wrong for a general AmigaOS mailer.

* Upgrade from v10.15, which is the previous public release. v10.16 and
  v10.17 were never released -- they are listed further down because
  they explain the version jump and because the AmigaOS problems they
  uncovered are worth passing on to anyone else porting to this
  platform. Nothing shipped between v10.15 and this release.

* The root cause, after three releases of narrowing it down, was
  setting a file's datestamp. On AmigaOS that is SetFileDate(), and
  when another Process holds the file it does not return an error --
  it never returns. No timeout bounds it, nothing watches the session,
  so the session is gone: it keeps its server slot, socket and .bsy
  locks forever. Enough of those and the server hits "maxservers" and
  refuses all inbound.

  It only bites when something else is touching the same directories.
  On the author's system that is C-Net/5's own overnight file-catalog
  jobs, which is why it looked like a night-time-only fault: 66 of 66
  inbound commits succeeded during the day, and 7 of 7 hung after
  21:00. One of those hangs landed inside a poll client, so the BBS
  event that launched it never finished and outbound mail was dead
  for seventeen hours.

* Fix: new "set-file-dates" setting, defaulting OFF on this port (see
  the manual, section 06). Every other binkd platform defaults it on;
  this is the one place AmiBinkD deliberately differs. What you give
  up is cosmetic -- received files carry their arrival time rather
  than the sender's timestamp. Nothing in BinkP, tossing or duplicate
  detection depends on it. Turn it on only if you are certain nothing
  else touches your inbound/outbound directories while the mailer
  runs.

* Verified over an 18-hour run spanning the overnight window that
  broke the previous release: 89 inbound sessions started, 89 closed,
  nothing leaked, 351 file commits, concurrent-session count peaking
  at 5 and returning to zero between polls, and all 59 scheduled BBS
  mail events completing. For comparison, v10.16 saturated in 13
  hours and v10.17 had leaked 55 sessions by 26 hours.

* Also fixed: "unknown domain" errors and silently-skipped polls. Not
  a bug you could have hit -- it existed only in an unreleased build
  -- but the cause is worth passing on, because the build system, not
  the code, was at fault. Object files did not depend on headers, so
  changing a shared struct rebuilt only some of the program and left
  the rest reading the old field offsets. The binary linked, ran, and
  quietly stopped calling out on six of seven networks. If you build
  this yourself from an older Makefile, do a full "make clean" after
  touching any header.

v10.17 - One Hung Session No Longer Takes The Others With It (unreleased)
------------------------------------------------------------------------

* Never released; superseded by v10.18. Sessions refresh their
  .bsy datestamps on every pass of the protocol loop, and that work
  was done holding a single global lock across file I/O. One session
  blocking in there pinned the lock and every other concurrent session
  piled up behind it -- so a single stuck file operation took down
  every session on the system, and the pile-up fed itself. v10.17
  stopped the cascade (the lock is never held across I/O now, and is
  never waited on), but a session could still hang on its own. v10.18
  removes the hanging call entirely.

v10.16 - Inbound Socket Handoff (unreleased; fix retained in v10.18)
-------------------------------------------------------------------

* Never released. Its inbound fix was real and is retained in v10.18,
  but on test it traded one failure for another: sessions that hung
  leaked permanently and piled up until inbound stopped, which in
  practice was worse than v10.15's quieter failure. That leak is what
  v10.17 and v10.18 went on to fix. The notes below are kept because
  the underlying problem is instructive for anyone porting to AmigaOS.

* Worth doing whatever mailer you run: if your BBS event script
  deletes .bsy/.csy files before polling, make sure a failed delete
  cannot abort the script -- on AmigaDOS, wrap them in "FAILAT 21"
  and "FAILAT 10". A lock file held by a live session is otherwise
  enough to kill the whole event, and your mailer never runs. The
  "kill-old-bsy" setting already removes stale locks for you.

* Inbound BinkP sessions never worked at all, in any release from
  v10.5 through v10.15.

  The symptom was easy to blame on the other end. Your log recorded
  "incoming session with <peer>" and then nothing further - no error,
  no timeout, no session close - while the caller gave up reporting
  "Session timeout" and "Authorization failed". Worse, a stuck session
  never freed its server slot, so the next caller was refused outright
  and inbound mail stopped within minutes of every startup. Restarting
  appeared to help, which made it look like an intermittent fault.
  Outbound polling was completely unaffected, so mail you fetched
  yourself kept arriving normally and the problem was easy to miss.

  Cause: AmigaOS gives each Process its own bsdsocket.library context.
  Since v10.5 every session runs as its own Process, and inbound
  sessions were using the parent's shared library base for a socket
  they did not own. Socket readiness is delivered by signal, and those
  signals never reached the child - so an inbound session could neither
  send nor receive, and simply sat there. v10.12 fixed the same class
  of problem for outbound connections but did not cover inbound.

  AmiBinkD now transfers the socket to the session's own private
  library base (ReleaseSocket/ObtainSocket), which is the documented
  AmigaOS way to hand a socket between Processes.

* Also resolved by the same fix: sessions ignoring the "timeout"
  setting. A stalled session used to linger indefinitely; it now closes
  on schedule. No configuration change is needed - if you added
  settings while working around the old behaviour, you can remove them.
* Do NOT set "maxservers" as a workaround for the old symptom. On this
  port a rejection can take the server manager down with it. Leaving it
  unset (default 100) is correct.
* Verified live: five consecutive inbound sessions from a network hub
  in 44 seconds - full handshake, CRAM-MD5 authentication, packets
  received, clean teardown each time, listener still accepting
  afterwards. Outbound polling unchanged in the same period.

v10.15 - Poll Every Network From One Config ("-P ALL"), Custom Icon
------------------------------------------------------------------

* New: "-P ALL" polls every node in the config in a single invocation:

    AmiBinkD:AmiBinkD -p -PALL AmiBinkD:AmiBinkD.cfg

  Stock binkd's -P takes one FTN address per switch, so carrying
  several networks meant either repeating -P once per uplink or
  keeping a separate config and driver script for each. One config
  and one command line now cover all of them; the inbound server
  ("-s") already used the combined config, so both sides finally read
  the same file. Nodes with no host to call, and your own addresses,
  are skipped rather than polled, and the log summarizes what was
  created ("ALL: created 7 poll(s)"). See manual.txt sections 07-09.
* Worth checking alongside it: "maxclients" now decides something it
  never did before. With every uplink pollable in one run, its default
  of 100 means all of them are called at once, where the one-script-
  per-network pattern was strictly sequential. See manual.txt section
  06.
* Confirmed live: a scheduled poll cycle logged "ALL: created 7
  poll(s)" and completed clean sessions with all seven networks.
* The icon work below was briefly released on its own as a
  packaging-only 10.15 carrying the v10.14 binary. "-P ALL" was folded
  into the same version number rather than becoming 10.16, so that
  10.15 isn't a version with nothing to point at. If your AmiBinkD10_15
  archive holds a binary that reports v10.14 at startup, it's that
  earlier cut -- replace it with this one.

* Replaced AmiBinkD.info -- previously a reused copy of another local
  project's icon -- with a purpose-made one: the Reign of Fire
  dragon-head badge, cropped to its circular emblem, quantized to a
  64-colour palette-mapped image, and alpha-masked to a circle so it
  sits cleanly on the Workbench desktop instead of carrying a black
  square background. Includes a distinct brighter "selected" image for
  the icon's clicked state.
* Built with icon.library's own V44+ API (NewDiskObject/IconControlA/
  PutIconTagList) instead of a hand-rolled on-disk format, so the OS's
  own code produces the file. See manual.txt section 18 for the full
  writeup and where the build tooling lives.
  Requires AmigaOS 3.5+ (icon.library V44) to render the colour image;
  older Workbench versions fall back to a generic tool icon.

v10.5-v10.14 - Real Concurrency, and the Hang It Exposed
-----------------------------------------------------------

Thirteen point releases' worth of work, summarized here; see manual.txt's
own Version History (section 18) for the full technical writeup of each.

* v10.5 added real concurrency: each BinkP session now runs as its own
  AmigaOS process via CreateNewProcTags(), instead of one session at a
  time synchronously. See manual.txt section 05.
* v10.6-v10.7 fixed two bugs concurrency itself exposed: a stale-lock
  give-up/backoff that wasn't actually being honored, and an errno race
  at a critical logging section under real concurrent inbound load.
* v10.8-v10.12 chased down and fixed the hardest bug in this project:
  every outbound poll would hang indefinitely, even though the real TCP
  connection completed in under a second every time. Two plausible-
  looking fixes (different techniques for detecting when a non-blocking
  connect() finishes) both failed live before the real cause was found:
  a blocking bsdsocket.library call's internal completion notification
  never reaches a spawned, non-opener AmigaOS process. Fixed by giving
  each outbound-connecting session its own private bsdsocket.library
  instance. See manual.txt section 05 and readme's counterpart, this
  project's README.md, "Real-hardware findings" #10, for the full story.
* v10.9/v10.13/v10.14 dealt with the same root cause showing up in
  gethostbyname() too. v10.9 worked around it with a host-side DNS
  cache; once v10.12's real fix landed, v10.13 confirmed (over a
  two-day, two-reboot soak) that the cache was no longer needed, and
  v10.14 removed it for good -- hostname resolution is a plain
  gethostbyname() call again, same as every other platform binkd
  supports.

v10.1 - .csy/.bsy Unlink Fix
------------------------------

* Fixed the .csy/.bsy busy-flag file occasionally failing to clean up
  after a session ("Text file busy") -- the cleanup now retries instead
  of giving up after one attempt. Cosmetic in v10.0 (never blocked a
  session either way), but the retry is more robust and matches how the
  rest of the codebase already handles this class of transient Amiga
  file-lock race. Verified live on production. See manual.txt section
  18 for the full technical writeup.

v10.0 - Native AmigaOS Port
----------------------------

* Rebuilt from a clean upstream binkd checkout with every ixemul/ixnet
  dependency replaced by native AmigaOS/bsdsocket.library equivalents,
  rather than patching an incomplete existing Amiga port.
* Runs each BinkP session synchronously (no fork()/pthreads needed) --
  a genuine, working single-session-at-a-time design.
* Found and fixed several real behavioral differences between
  Amiberry's bsdsocket_emu and genuine BSD sockets that only surfaced
  under live testing (getaddrinfo(), non-blocking I/O, accept()
  blocking after a ready select(), WaitSelect() signal handling) -- see
  manual.txt section 15 for the full list.
* Verified two ways before shipping: cross-compiled clean (zero
  warnings) with the bebbo/amiga-gcc toolchain, and run live on a
  production Amiberry instance -- confirmed both outbound (client/poll)
  and inbound (server) sessions, including a full multi-network polling
  run (7 networks) with real inbound file reception and successful
  tosser import, not just clean-looking session logs.
* Soak-tested unattended overnight across all 7 configured networks with
  no hangs, no gaps, and no recurrence of any fixed bug.

For the full technical history -- every ixemul-era assumption replaced,
every real-hardware finding, and why each fix works the way it does --
see manual.txt sections 15 and 18.


SECURITY NOTES
===============================================================================

AmiBinkD accepts inbound TCP connections and moves files onto your
system based on what a remote FTN node sends it.

Recommended precautions:

* Set a real per-node password (areafix password) for every "node" line
  -- don't leave a node unauthenticated unless you mean to.
* Use send-if-pwd (on by default in the example config) so an
  unauthenticated session can only receive, not pull mail from you.
* Keep your inbound/outbound directories separate from directories your
  BBS treats as directly executable or auto-processed without review.
* Review your log regularly.
* Keep backups.


DISCLAIMER
===============================================================================

AmiBinkD is provided as-is. It is a from-scratch reconstruction of
ixemul-dependent Amiga binkd behavior verified against one real system
(Amiberry-emulated AmigaOS 3.x, bsdsocket_emu) -- other bsdsocket.library
implementations (real Roadshow, AmiTCP) have not been tested and may
behave differently; see manual.txt section 15 before assuming a finding
there applies to your setup.

Every FTN network and BBS system is different. Test carefully before
using this on a live system carrying real mail.

The author is not responsible for lost mail, misconfigured domains/
addresses, missing passwords, or damage caused by improper setup.

Always keep backups.


CREDITS
===============================================================================

AmiBinkD is a native AmigaOS port of binkd
(https://github.com/pgul/binkd), originally written by Dima Maloff and
maintained by the binkd project. All BinkP protocol logic, FTN packet
handling, and configuration semantics are upstream binkd's; this port's
own work is the AmigaOS/bsdsocket.library integration layer and the
real-hardware fixes documented in manual.txt.

Earlier Amiga port (AmiBinkd v5.00 - v9.02, 2012-2013):

Rudi Timmermans, X-TReMe BBS

AmiBinkD carried this name on the Amiga long before the present release.
Rudi Timmermans built and maintained it through v9.02, made it freeware
at v5.00, and got a working BinkP mailer onto classic AmigaOS at a time
when that meant living with ixemul.library. This port is a from-scratch
reconstruction rather than a recompile of his -- the ixemul dependency is
gone and every session now runs as a native AmigaOS process -- but the
program's name, its configuration file format, and the shape of its
session log all come from his work, and the log is still recognisably the
same one his v9.01 produced in 2013. Credit where it is due.

Built and tested for:

Reign of Fire BBS
C-Net/5 Amiga Pro
call.rofbbs.com:6800

Website:

https://www.rofbbs.com

SysOp / Author (Amiga port):

Gary "Spitfire" McCulloch

Networks tested against:

AmigaNet, ArakNet, CommodoreNet, DiscipleNet, FidoNet, PiNet, RetroNet

===============================================================================
END OF README.TXT                                      SpitfireTN Entertainment
===============================================================================
