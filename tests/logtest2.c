/* logtest2 -- the same append test, but with the writers as PROCESSES IN ONE
 * ADDRESS SPACE, the way AmiBinkd's sessions actually run.
 *
 * logtest (five separate CLI programs) produced 2,390 writes with ZERO torn
 * lines, so fopen(path,"a") under a lock is not the problem and fseek() is
 * not the fix. The one structural difference left between that test and
 * production is this:
 *
 *     logtest      5 programs   -> 5 address spaces, 5 private stdio states
 *     AmiBinkd     branch()     -> N Processes sharing ONE address space
 *
 * libnix's stdio keeps its state per-PROGRAM, not per-Process. If two
 * Processes in one address space both run fopen/fprintf/fclose, they may be
 * sharing FILE structures and buffers. That would explain both observed
 * corruptions: a log line written at byte 0 over existing content, and a
 * fragment of a log line found inside a .bsy lock file.
 *
 * This spawns its writers exactly the way branch.c does -- same
 * CreateNewProcTags call, same trampoline via tc_UserData, same 256KB stack,
 * same NP_WindowPtr -1 -- so if the shared address space is the mechanism,
 * it reproduces here.
 *
 * Usage:  logtest2 <path> <writers 1-9> <count> <mode 0|1>
 *
 * Verify with tests/check_logtest.py (expects writers*count lines, each
 * exactly 64 chars, every (id,seq) present once).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* Same workaround branch.c needs: this toolchain ships an empty
 * dos/dostags.h stub and no CreateNewProcTags() prototype, so TAG_DONE,
 * TAG_USER and the prototype never arrive via the includes above. */
#ifndef TAG_DONE
#define TAG_DONE ((ULONG) 0)
#endif
#ifndef TAG_USER
#define TAG_USER ((ULONG) (1UL << 31))
#endif
/* NP_* come from dos/dostags.h. Verified by preprocessing with these exact
 * includes: NP_Entry = TAG_USER+1000+3, StackSize +11, Name +12, Priority
 * +13, WindowPtr +15. Deliberately NOT redefined here behind an #ifndef --
 * guessed tag values that silently win if the include path shifts would hand
 * CreateNewProcTags garbage on a live machine. */
struct Process * __stdargs CreateNewProcTags (ULONG tag1type, ...);

#define SEM_NAME  "AmiBinkdLogTest2"
#define LINE_LEN  64
#define STACKSIZE (256*1024)          /* same as Config.h */

typedef struct
{
    const char *path;
    int id, count, mode;
} writer_args_t;

static struct SignalSemaphore *g_sem   = NULL;   /* shared: one address space */
static volatile LONG           g_alive = 0;      /* children still running    */

static struct SignalSemaphore *test_sem (void)
{
    struct SignalSemaphore *sem;

    Forbid ();
    sem = (struct SignalSemaphore *) FindSemaphore ((STRPTR) SEM_NAME);
    if (sem == NULL)
    {
        sem = (struct SignalSemaphore *)
              AllocMem (sizeof (struct SignalSemaphore), MEMF_PUBLIC | MEMF_CLEAR);
        if (sem != NULL)
        {
            InitSemaphore (sem);
            sem->ss_Link.ln_Name = (char *) SEM_NAME;
            sem->ss_Link.ln_Pri  = 0;
            AddSemaphore (sem);
        }
    }
    Permit ();
    return sem;
}

/* One writer. Mirrors tools.c's Log() write path exactly. */
static void writer_body (void *arg)
{
    writer_args_t *wa = (writer_args_t *) arg;
    char line[LINE_LEN + 2];
    char filler[45];
    int seq, i;

    memset (filler, 'A' + (wa->id % 26), 44);
    filler[44] = '\0';

    for (seq = 0; seq < wa->count; ++seq)
    {
        FILE *fp = NULL;

        ObtainSemaphore (g_sem);

        for (i = 0; fp == NULL && i < 10; ++i)
            fp = fopen (wa->path, "a");

        if (fp)
        {
            if (wa->mode == 1)
                fseek (fp, 0, SEEK_END);
            snprintf (line, sizeof line, "LINE id=%02d seq=%04d %s",
                      wa->id, seq % 10000, filler);
            fprintf (fp, "%s\n", line);
            fclose (fp);
        }

        ReleaseSemaphore (g_sem);
    }

    free (wa);

    Forbid ();
    g_alive--;
    Permit ();
}

/* Same shape as branch.c's amiga_proc_trampoline(). */
static void trampoline (void)
{
    struct Task *me = FindTask (NULL);
    writer_args_t *wa = (writer_args_t *) me->tc_UserData;

    me->tc_UserData = NULL;
    writer_body (wa);
}

int main (int argc, char **argv)
{
    const char *path;
    int writers, count, mode, i, waited = 0;
    struct Task *me = FindTask (NULL);

    if (argc < 5)
    {
        printf ("usage: logtest2 <path> <writers 1-9> <count> <mode 0|1>\n");
        return 20;
    }
    path    = argv[1];
    writers = atoi (argv[2]);
    count   = atoi (argv[3]);
    mode    = atoi (argv[4]);
    if (writers < 1 || writers > 9)
    {
        printf ("writers must be 1-9\n");
        return 20;
    }

    if ((g_sem = test_sem ()) == NULL)
    {
        printf ("could not create semaphore\n");
        return 20;
    }

    printf ("logtest2: spawning %d writers x %d lines, mode %d, ONE address space\n",
            writers, count, mode);

    for (i = 0; i < writers; ++i)
    {
        writer_args_t *wa = (writer_args_t *) malloc (sizeof (*wa));
        struct Process *p;

        if (wa == NULL)
            continue;
        wa->path  = path;
        wa->id    = i;
        wa->count = count;
        wa->mode  = mode;

        Forbid ();
        g_alive++;
        Permit ();

        p = CreateNewProcTags (
                NP_Entry,     (ULONG) trampoline,
                NP_StackSize, (ULONG) STACKSIZE,
                NP_Priority,  (LONG) (me->tc_Node.ln_Pri - 1),
                NP_Name,      (ULONG) "logtest2 writer",
                NP_WindowPtr, (ULONG) -1,
                TAG_DONE);

        if (p == NULL)
        {
            Forbid ();
            g_alive--;
            Permit ();
            free (wa);
            printf ("  writer %d: CreateNewProcTags failed\n", i);
        }
        else
            ((struct Task *) p)->tc_UserData = (APTR) wa;
    }

    /* wait for them, with a ceiling so a wedged child cannot hang the shell */
    while (g_alive > 0 && waited < 600)
    {
        Delay (25);                    /* 1/2 second */
        waited++;
    }

    printf ("logtest2: done, %ld writer(s) still alive after %d ticks\n",
            (long) g_alive, waited);
    return 0;
}
