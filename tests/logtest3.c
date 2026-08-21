/* logtest3 -- does writing to one file land bytes in ANOTHER file?
 *
 * Four tests so far (5 programs / 5 Processes in one address space, both
 * with and without fseek) produced ~4,400 concurrent writes with ZERO torn
 * lines. The write path is sound. But all four hammered a SINGLE file, and
 * production does not:
 *
 *     create_sem_file()  open .bsy  -> write 11 bytes -> close
 *     Log()              open log   -> write one line -> close
 *     ... per session, per file transferred, continuously
 *
 * The observed corruption was a .bsy that held its correct 11-byte pid AND
 * 55 further bytes of LOG TEXT. create_sem_file() writes strlen(buf) and
 * closes, so it did not put them there -- something else wrote through a
 * descriptor still pointing at that file. Amiberry is known to cache the
 * host fd of an actively-written file (see the log-tearing at byte 0).
 *
 * This reproduces the interleaving: each writer alternates between its OWN
 * lock file (11-byte marker, create/write/close, like create_sem_file) and a
 * SHARED log file (64-char line, append/write/close, like Log()). Then the
 * host checks for cross-contamination in both directions.
 *
 * Usage:  logtest3 <base> <writers 1-9> <rounds> <mode 0|1>
 *
 *   mode 0  lock file created/written/closed OUTSIDE the semaphore, the way
 *           create_sem_file() runs today while Log() holds its own lock.
 *           This is the reproducer: 92/1000 log lines lost, 4 LOCK markers
 *           written into the log, .lck files accumulating ~93 markers.
 *
 *   mode 2  THE CANDIDATE FIX: the log handle is opened ONCE and kept open
 *           for the whole run, shared by every writer Process, with an
 *           fflush() after each line. Lock files still churn exactly as in
 *           mode 0. If the log stops being corrupted, then taking the log
 *           out of the open/close cycle is enough, and the same change is
 *           available in tools.c.
 *
 *           The hazard being tested as much as the fix: a FILE * opened by
 *           one Process and written by others. They share one address space
 *           so the structure is reachable, and AmigaDOS file handles are not
 *           Process-bound -- but if libnix tears down streams when a child
 *           Process ends, this is a use-after-free. That is exactly why it
 *           is tried HERE and not in the mailer.
 *
 *   mode 1  DISABLED -- deadlocks: the lock file's create/write/close is taken
 *           under the SAME semaphore as the log write, so no two Processes
 *           ever have file opens in flight at once. If the misrouting stops,
 *           the real fix is a global file-op lock (or holding the log handle
 *           open instead of open/close per line, which cuts the churn).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <exec/types.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* Same workaround branch.c documents: this toolchain does not reliably
 * deliver TAG_DONE/TAG_USER here. NP_* still come from dos/dostags.h. */
#ifndef TAG_DONE
#define TAG_DONE ((ULONG) 0)
#endif
#ifndef TAG_USER
#define TAG_USER ((ULONG) (1UL << 31))
#endif
struct Process * __stdargs CreateNewProcTags (ULONG tag1type, ...);

/* The semaphore name is derived from the run's base name, NOT fixed.
 *
 * 2026-08-20/21: mode 1 deadlocked with writers blocked inside
 * ObtainSemaphore. An Exec public semaphore lives in a system list and
 * OUTLIVES the program that created it -- so it stayed locked, and every
 * later run that called FindSemaphore("AmiBinkdLogTest3") blocked on the
 * first ObtainSemaphore before creating a single file. Two runs wedged a
 * shell and tested nothing at all.
 *
 * A per-run name means a poisoned semaphore can never affect a later run.
 * The dead one leaks until reboot; it is ~46 bytes and harmless. */
#define SEM_PREFIX "AmiBinkdLT-"
#define STACKSIZE (256*1024)

typedef struct { const char *base; int id, rounds, mode; } wargs_t;

static struct SignalSemaphore *g_sem   = NULL;
static volatile LONG           g_alive = 0;
static FILE                   *g_log   = NULL;   /* mode 2: shared, kept open */

static char g_semname[64];

static struct SignalSemaphore *test_sem (void)
{
    struct SignalSemaphore *s;
    Forbid ();
    s = (struct SignalSemaphore *) FindSemaphore ((STRPTR) g_semname);
    if (s == NULL)
    {
        s = (struct SignalSemaphore *)
            AllocMem (sizeof (struct SignalSemaphore), MEMF_PUBLIC | MEMF_CLEAR);
        if (s)
        {
            InitSemaphore (s);
            s->ss_Link.ln_Name = g_semname;
            AddSemaphore (s);
        }
    }
    Permit ();
    return s;
}

static void writer_body (void *arg)
{
    wargs_t *w = (wargs_t *) arg;
    char lock[256], log[256], line[80], mark[24], filler[45];
    int r, h;
    FILE *fp;

    snprintf (lock, sizeof lock, "%s-%d.lck", w->base, w->id);
    snprintf (log,  sizeof log,  "%s.log", w->base);
    memset (filler, 'A' + (w->id % 26), 44);
    filler[44] = '\0';

    for (r = 0; r < w->rounds; ++r)
    {
        /* 1. the .bsy-style file: exclusive create, 11-byte marker, close.
         *    In mode 1 this is taken under the SAME semaphore as the log
         *    write below, so no other Process can have a file open in
         *    flight while this one is opening/closing. */
        if (w->mode == 1)
            ObtainSemaphore (g_sem);

        unlink (lock);
        if ((h = open (lock, O_RDWR | O_CREAT | O_EXCL, 0666)) != -1)
        {
            snprintf (mark, sizeof mark, "LCK%02d%05d\n", w->id, r % 100000);
            write (h, mark, strlen (mark));
            close (h);
        }

        if (w->mode == 1)
            ReleaseSemaphore (g_sem);

        /* 2. the Log()-style write */
        snprintf (line, sizeof line, "LINE id=%02d seq=%04d %s",
                  w->id, r % 10000, filler);

        ObtainSemaphore (g_sem);
        if (w->mode == 2)
        {
            /* open once, keep it, flush every line */
            if (g_log == NULL)
                g_log = fopen (log, "a");
            if (g_log)
            {
                fprintf (g_log, "%s\n", line);
                fflush (g_log);
            }
        }
        else
        {
            if ((fp = fopen (log, "a")) != NULL)
            {
                fprintf (fp, "%s\n", line);
                fclose (fp);
            }
        }
        ReleaseSemaphore (g_sem);
    }

    free (w);
    Forbid (); g_alive--; Permit ();
}

static void trampoline (void)
{
    struct Task *me = FindTask (NULL);
    wargs_t *w = (wargs_t *) me->tc_UserData;
    me->tc_UserData = NULL;
    writer_body (w);
}

int main (int argc, char **argv)
{
    int writers, rounds, mode, i, waited = 0;
    struct Task *me = FindTask (NULL);

    if (argc < 5)
    { printf ("usage: logtest3 <base> <writers 1-9> <rounds> <mode 0|1>\n");
      return 20; }
    writers = atoi (argv[2]);
    rounds  = atoi (argv[3]);
    mode    = atoi (argv[4]);
    if (writers < 1 || writers > 9) { printf ("writers 1-9\n"); return 20; }

    if (mode == 1)
    {
        /* DISABLED 2026-08-20: mode 1 DEADLOCKS and wedged a live shell.
         * It holds the semaphore across unlink()/open(), and this project
         * already established (v10.18, touch()/SetFileDate) that AmigaOS
         * file calls block FOREVER when another Process holds the object --
         * they do not return an error, they never return. One writer stuck
         * in unlink() blocks every other writer behind the semaphore.
         *
         * Serializing file opens is still the right idea, but it cannot be
         * done by wrapping a lock around a call that can block indefinitely.
         * The direction to try instead is REDUCING descriptor churn: hold
         * the log handle open rather than fopen/fclose per line. */
        printf ("mode 1 is disabled -- it deadlocks. See the comment in\n");
        printf ("tests/logtest3.c. Use mode 0 to reproduce the corruption.\n");
        return 20;
    }

    /* unique per base name, so a wedged run cannot poison the next one */
    snprintf (g_semname, sizeof g_semname, SEM_PREFIX "%s", argv[1]);
    printf ("logtest3: semaphore \"%s\"\n", g_semname);

    if ((g_sem = test_sem ()) == NULL) { printf ("no semaphore\n"); return 20; }

    printf ("logtest3: %d writers x %d rounds, mode %d (%s)\n",
            writers, rounds, mode,
            mode == 1 ? "lock ops SERIALIZED with log" : "lock ops unserialized");

    for (i = 0; i < writers; ++i)
    {
        wargs_t *w = (wargs_t *) malloc (sizeof (*w));
        struct Process *p;
        if (!w) continue;
        w->base = argv[1]; w->id = i; w->rounds = rounds; w->mode = mode;
        Forbid (); g_alive++; Permit ();
        p = CreateNewProcTags (NP_Entry,     (ULONG) trampoline,
                               NP_StackSize, (ULONG) STACKSIZE,
                               NP_Priority,  (LONG) (me->tc_Node.ln_Pri - 1),
                               NP_Name,      (ULONG) "logtest3 writer",
                               NP_WindowPtr, (ULONG) -1,
                               TAG_DONE);
        if (p == NULL)
        { Forbid (); g_alive--; Permit (); free (w);
          printf ("  writer %d: spawn failed\n", i); }
        else
            ((struct Task *) p)->tc_UserData = (APTR) w;
    }

    while (g_alive > 0 && waited < 900) { Delay (25); waited++; }

    if (g_log) { fclose (g_log); g_log = NULL; }   /* parent owns the close */
    printf ("logtest3: done, %ld alive after %d ticks\n", (long) g_alive, waited);
    return 0;
}
