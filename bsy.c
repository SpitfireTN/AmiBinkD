/*
 * You must be VERY CAREFUL with this module. Note, this
 * code is working in VERY diff. ways in forking vs. threading versions!!
 */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "sys.h"
#include "readcfg.h"
#include "bsy.h"
#include "ftnaddr.h"
#include "ftndom.h"
#include "sem.h"
#include "tools.h"
#include "iphdr.h"
#include "assert.h"
#include "readdir.h" /* for rmdir() */

#if defined(HAVE_THREADS) || defined(AMIGA)
static MUTEXSEM sem;	/* =0 initializer fails for amiga. removed. useless anyway? */
#endif

typedef struct _BSY_ADDR BSY_ADDR;
struct _BSY_ADDR
{
  BSY_ADDR *next;
  FTN_ADDR fa;
  bsy_t bt;
#if !defined(UNIX) && !defined(AMIGA)
  int h;
#endif
};

BSY_ADDR *bsy_list = 0;

void bsy_init (void)
{
  InitSem (&sem);
}

void bsy_deinit (void)
{
  CleanSem (&sem);
}

static BSY_ADDR *bsy_get_free_cell (void)
{
  BSY_ADDR *lst;

  for (lst = bsy_list; lst; lst = lst->next)
    if (FA_ISNULL (&lst->fa))
      break;
  if (!lst)
  {
    lst = xalloc (sizeof (BSY_ADDR));
    FA_ZERO (&lst->fa);
    lst->next = bsy_list;
    bsy_list = lst;
  }
  return lst;
}


#ifdef DIAG_BSY
/*
 * TEMPORARY (2026-08-15) -- pinpoints where a stalled session dies, then goes.
 *
 * A poll froze for 2h38m on 2026-08-15 with its last log line being the M_ADR
 * handler's address list. The socket sat CLOSE-WAIT with 5,550 bytes unread for
 * 49 minutes, and DIAG_SPIN never fired -- so the protocol main loop was not
 * running at all and the task was blocked inside address processing, which is
 * exactly where bsy_add() runs once per remote AKA.
 *
 * Two candidates inside bsy_add(), and these markers tell them apart:
 *   want-lock -> (silence)  = blocked on the GLOBAL bsy semaphore, i.e. some
 *                             sibling Process is holding it across file I/O.
 *                             Same shape as the bsy_touch() ratchet.
 *   creating  -> (silence)  = blocked in create_sem_file()'s open(), i.e. a
 *                             single object the emulator will not let go.
 * Whichever line is last names the failure.
 */
#define DIAG_BSY_LOG(...) Log (4, __VA_ARGS__)
#else
#define DIAG_BSY_LOG(...) do { } while (0)
#endif

int bsy_add (FTN_ADDR *fa0, bsy_t bt, BINKD_CONFIG *config)
{
  char buf[MAXPATHLEN + 1];
  int ok = 0;

  ftnaddress_to_filename (buf, fa0, config);

  DIAG_BSY_LOG ("DIAG-BSY: add want-lock %s `%s'", bt == F_CSY ? "csy" : "bsy", buf);
  LockSem (&sem);
  DIAG_BSY_LOG ("DIAG-BSY: add got-lock");
  if (*buf)
  {
    strnzcat (buf, bt == F_CSY ? ".csy" : ".bsy", sizeof (buf));
    DIAG_BSY_LOG ("DIAG-BSY: add mkpath `%s'", buf);
    if (mkpath (buf) == -1)
      Log (1, "mkpath('%s'): %s", buf, strerror (errno));

    DIAG_BSY_LOG ("DIAG-BSY: add creating `%s'", buf);
    if (create_sem_file (buf, 5))
    {
      BSY_ADDR *new_bsy = bsy_get_free_cell ();

      memcpy (&new_bsy->fa, fa0, sizeof (FTN_ADDR));

      new_bsy->bt = bt;

/* AmigaOS is grouped with UNIX for the .bsy/.csy handle below.
 *
 * Every other non-UNIX platform keeps the lock file open (BSY_ADDR.h) and
 * relies on that handle to stop anyone else deleting it. That cannot work
 * here. AmigaOS file handles are per-Process, exactly like sockets, and
 * since v10.5 each session is its own Process while bsy_list is a plain
 * global shared by all of them (classic AmigaOS has one flat address
 * space -- no fork() copy-on-write). So a Process closing bsy->h for a
 * cell some *other* Process created closes an unrelated descriptor in its
 * own table; the real handle stays open, and AmigaOS will not delete an
 * open file. That is the routine
 *
 *     error unlinking `...bsy': Text file busy
 *
 * left behind after concurrent inbound sessions -- and those leftover
 * locks are what made CNet's FTN_Poll script abort at its `Delete #?.bsy'
 * step, taking outbound mail down with them.
 *
 * UNIX has never kept the handle, because the exclusive create_sem_file()
 * IS the lock. That reasoning holds identically on AmigaOS, so take the
 * same path rather than inventing a per-Process handle table. */
#if !defined(UNIX) && !defined(AMIGA)
      new_bsy->h = open(buf, O_RDONLY|O_NOINHERIT);
      if (new_bsy->h == -1)
        Log (2, "Can't open %s: %s!", buf, strerror(errno));
#if defined(OS2)
      else
        DosSetFHState(new_bsy->h, OPEN_FLAGS_NOINHERIT);
#elif defined(EMX)
      else
        fcntl(new_bsy->h,  F_SETFD, FD_CLOEXEC);
#endif
#endif

      ok = 1;
    }
  }
  DIAG_BSY_LOG ("DIAG-BSY: add done, releasing lock");
  ReleaseSem (&sem);
  return ok;
}

/*
 * Test a busy-flag. 1 -- free, 0 -- busy
 */
int bsy_test (FTN_ADDR *fa0, bsy_t bt, BINKD_CONFIG *config)
{
  char buf[MAXPATHLEN + 1];

  ftnaddress_to_filename (buf, fa0, config);
  if (*buf)
  {
    strnzcat (buf, bt == F_CSY ? ".csy" : ".bsy", sizeof (buf));

    if (mkpath (buf) == -1)
      Log (1, "mkpath('%s'): %s", buf, strerror (errno));

    if (access (buf, F_OK) == -1)
      return 1;
  }
  return 0;
}

/*
 * v10.20: remove one of OUR OWN lock files, without the 4-second stall.
 *
 * bsy_remove() runs at session end (protocol.c:290) once per remote AKA, and
 * sdelete() retries five times with sleep(1) between -- up to 4 seconds per
 * lock. Measured on 2026-08-13: a session logged "done" at :04, then unlink
 * errors at :09 and :15, then "session closed" at :15. Eleven seconds of pure
 * sleeping after the transfer had already finished, on every session.
 *
 * We cannot just drop the retry the way process_bsy() did. That one deletes
 * a foreign, already-stale lock; this one deletes ours, and a leftover .bsy
 * marks the node busy -- q_next_node() (ftnq.c) skips busy nodes -- until
 * kill-old-bsy ages it out, which is 2h in this configuration. Trading 11
 * seconds of delay for a 2-hour polling outage on that node would be a bad
 * bargain.
 *
 * So keep the retry and cut the granularity instead. Precedent: the log file
 * had the same problem and the same shape of fix (see amiga/msleep.c) -- ten
 * back-to-back retries all failed because a task holding the object for even
 * a few ms defeated them, while a few tens of milliseconds of delay was
 * enough. 10 tries x 40ms is 400ms worst case instead of 4s, with more
 * attempts than before rather than fewer.
 */
#define BSY_UNLINK_TRIES 10

static int bsy_unlink (char *path)
{
  int rc = -1;
#ifdef AMIGA
  int i;

  for (i = 0; i < BSY_UNLINK_TRIES; i++)
  {
    if (i)
      LOG_RETRY_DELAY ();               /* amiga_msleep(40) */
    if ((rc = UNLINK (path)) == 0)
    {
      Log (6, "unlinked `%s'", path);
      return 0;
    }
    if (!(errno == EPERM || errno == EACCES || errno == EAGAIN || errno == ETXTBSY))
      break;
  }

  /* v10.28: quieten a race that is not a fault.
   *
   * Measured over 12 hours: 106 of these logged, yet Echomail:Outbound held
   * only the two locks of the current cycle -- the files ARE being removed,
   * just not always by the Process that complains. binkd runs each session as
   * its own AmigaOS Process (branch.c), and five of them were seen racing to
   * clear the same .bsy. The loser logs an error for work the winner already
   * did.
   *
   * ENOENT is that race, plainly: the lock is gone, which is the outcome we
   * wanted. Nothing is wrong, so say nothing. 18 of the 106 were this.
   *
   * The rest are dropped from level 1 to level 3. Level 1 is for things that
   * need attention, and this does not: the lock expires under kill-old-bsy,
   * the directory does not accumulate, and no mail is lost. Keeping it at
   * level 1 trained the eye to skim real errors.
   *
   * The AmigaDOS code that v10.27 added is NOT printed here any more. It
   * reported 0 on all 44 samples -- impossible, since IoErr()==0 maps to EIO
   * while every message said EACCES. amiga_last_ioerr is a static, and these
   * are separate Processes sharing one address space, so concurrent writers
   * corrupt it. Same defect class as mypid, fixed in v10.26. Left in
   * amiga/delete.c but no longer trusted or shown. */
  if (errno == ENOENT)
    return 0;                    /* another Process won the race: job done */

  Log (3, "could not remove own lock `%s': %s", path, strerror (errno));
#else
  rc = sdelete (path);
#endif
  return rc;
}

void bsy_remove (FTN_ADDR *fa0, bsy_t bt, BINKD_CONFIG *config)
{
  char buf[MAXPATHLEN + 1], *p;
  BSY_ADDR *bsy;

  ftnaddress_to_filename (buf, fa0, config);
  if (*buf)
  {
    strnzcat (buf, bt == F_CSY ? ".csy" : ".bsy", sizeof (buf));

    DIAG_BSY_LOG ("DIAG-BSY: remove want-lock `%s'", buf);
    LockSem (&sem);
    DIAG_BSY_LOG ("DIAG-BSY: remove got-lock");
    for (bsy = bsy_list; bsy; bsy = bsy->next)
    {
      if (!ftnaddress_cmp (&bsy->fa, fa0) && bsy->bt == bt)
      {
#if !defined(UNIX) && !defined(AMIGA)
	if (bsy->h != -1)
	  if (close(bsy->h))
            Log (2, "Can't close %s (handle %d): %s!", buf, bsy->h, strerror(errno));
#endif
	bsy_unlink (buf);
	/* remove empty point directory */
	if (config->deletedirs)
	{
	  FTN_DOMAIN *d;
	  if (fa0->p != 0 && (p = last_slash(buf)) != NULL)
	  {
	    *p = '\0';
	    rmdir(buf);
	  }
	  /* remove empty zone directory */
	  d = get_domain_info (fa0->domain, config->pDomains.first);
	  if (d && (fa0->z != d->z[0]) && (p = last_slash(buf)) != NULL)
	  {
	    *p = '\0';
	    rmdir(buf);
	  }
	}
	FA_ZERO (&bsy->fa);
	break;
      }
    }
    ReleaseSem (&sem);
  }
}

/*
 * For exitlist...
 */
void bsy_remove_all (BINKD_CONFIG *config)
{
  char buf[MAXPATHLEN + 1], *p;
  BSY_ADDR *bsy;

  for (bsy = bsy_list; bsy; bsy = bsy->next)
  {
    if (FA_ISNULL (&bsy->fa)) continue; /* free cell */
    ftnaddress_to_filename (buf, &bsy->fa, config);
    if (*buf)
    {
      strnzcat (buf, bsy->bt == F_CSY ? ".csy" : ".bsy", sizeof (buf));
#if !defined(UNIX) && !defined(AMIGA)
      if (bsy->h != -1)
        if (close(bsy->h))
          Log (2, "Can't close %s (handle %d): %s!", buf, bsy->h, strerror(errno));
#endif
      bsy_unlink (buf);
      /* remove empty point directory */
      if (config->deletedirs && bsy->fa.p != 0 && (p = last_slash(buf)) != NULL)
      {
	*p = '\0';
	rmdir(buf);
      }

      FA_ZERO (&bsy->fa);
    }
  }
  Log (6, "bsy_remove_all: done");
  bsy_deinit ();
}

/*
 * Touchs all our .bsy's if needed
 */
void bsy_touch (BINKD_CONFIG *config)
{
  static time_t last_touch = 0;
  BSY_ADDR *cur;

  /* Cheap check BEFORE taking the lock.
   *
   * Every session calls this on every pass of the protocol main loop, so
   * with N concurrent sessions it was N global-semaphore acquisitions per
   * pass purely to discover there is nothing to do yet -- the timer test
   * used to live inside the critical section. On AmigaOS that contention
   * was enough to strand sessions here: instrumentation on 2026-08-07
   * recorded 251 entries to bsy_touch() against 152 exits, i.e. 99
   * sessions went in and never came out, while SELECT() immediately
   * before it balanced perfectly (250 in / 251 out).
   *
   * It is self-amplifying, which matches the observed curve: stranded
   * sessions keep their .bsy entries, so bsy_list grows, so each pass
   * holds the lock longer, so more sessions strand. Skipping the lock
   * entirely on the ~99.9% of calls that have no work removes it.
   *
   * Safe as double-checked locking: last_touch is only advanced under the
   * lock, and the worst case of a stale read is touching the files one
   * pass early or late, which BSY_TOUCH_DELAY already tolerates. */
  if (time (0) - last_touch <= BSY_TOUCH_DELAY)
    return;

  /* Nothing to do at all if datestamping is off: every useful thing this
   * function does is a touch(). Bail before walking the list rather than
   * spinning the lock for a no-op. See readcfg.c for why AmigaOS defaults
   * set-file-dates off -- touch() there can block forever, and this
   * function runs on every pass of every session's protocol main loop,
   * which made it the single most exposed caller in the program. */
  if (!config->set_file_dates)
  {
    last_touch = time (0);
    return;
  }

  /* The double-checked lock above was not enough on its own (measured
   * again 2026-08-08: 236 entries to 136 exits, 100 sessions stranded
   * here, while the SELECT() immediately before balanced 236/235). Cutting
   * the *number* of acquisitions does nothing about the shape of the bug:
   * this is a single global semaphore held across filesystem I/O, so one
   * session that blocks inside touch() -- or inside the Log() next to it,
   * on a filesystem that routinely returns ETXTBSY here -- pins the
   * semaphore, and every sibling then blocks forever in ObtainSemaphore()
   * on the next pass of its protocol main loop. One stuck file operation
   * takes down every concurrent session, which is exactly the observed
   * ratchet: n_servers climbs monotonically until it hits max_servers and
   * all inbound is refused.
   *
   * Two changes, either of which alone would break that chain:
   *
   *   1. Never block acquiring this lock. Touching .bsy files is periodic
   *      cosmetic maintenance (it stops peers ageing our locks out); a
   *      skipped pass costs nothing, and BSY_TOUCH_DELAY already tolerates
   *      being early or late. AttemptSemaphore() over ObtainSemaphore().
   *
   *   2. Do not hold the lock across the I/O at all. Claim the pass, copy
   *      out what is needed, release, and touch the files unlocked.
   *
   * With both, a session that hangs in touch() strands only itself and
   * leaves the semaphore free, instead of taking every sibling with it. */
  if (!TryLockSem (&sem))
    return;

  if (time (0) - last_touch <= BSY_TOUCH_DELAY)
  {                                     /* someone else claimed this pass */
    ReleaseSem (&sem);
    return;
  }

  /* Claim the pass before any I/O, so siblings take the cheap early-out at
   * the top of this function rather than queueing up behind us. */
  last_touch = time (0);
  cur = bsy_list;
  ReleaseSem (&sem);

  /* Walk unlocked. Cells are never freed -- bsy_get_free_cell() reuses
   * FA_ZERO'd ones -- so `cur' stays valid across the unlocked window, and
   * bsy_add() only ever prepends, so it cannot splice anything into the
   * part of the list still ahead of us. A cell that a concurrent
   * bsy_remove() clears while we are walking just gets skipped. */
  for (; cur; cur = cur->next)
  {
    char buf[MAXPATHLEN + 1];
    FTN_ADDR fa;
    bsy_t bt;

    if (!TryLockSem (&sem))
      return;                   /* contended: drop the rest of this pass */
    if (FA_ISNULL (&cur->fa))
    {
      ReleaseSem (&sem);
      continue;
    }
    memcpy (&fa, &cur->fa, sizeof (fa));
    bt = cur->bt;
    ReleaseSem (&sem);

    ftnaddress_to_filename (buf, &fa, config);
    if (*buf)
    {
      strnzcat (buf, bt == F_CSY ? ".csy" : ".bsy", sizeof (buf));
      if (touch (buf, time (0)) != -1)
        Log (6, "touched %s", buf);
      /* Touching unlocked means a concurrent bsy_remove() can sdelete()
       * this file between the copy above and the touch. That race is
       * benign and expected -- the lock is gone precisely so it can be --
       * so don't report it at the level real touch failures use. */
      else if (errno == ENOENT)
        Log (6, "touch %s: gone (removed concurrently)", buf);
      else
        Log (1, "touch %s: %s", buf, strerror (errno));
    }
  }
}
