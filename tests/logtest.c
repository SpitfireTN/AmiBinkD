/* logtest -- does fopen(path,"a") actually append under AmigaOS/libnix13?
 *
 * Reproduces AmiBinkD's Log() write path exactly (tools.c ~line 336):
 *
 *      LockSem   -> fopen(path,"a") -> one fprintf -> fclose -> ReleaseSem
 *
 * with the SAME public-semaphore mechanism, under its own semaphore name so
 * it cannot contend with a running AmiBinkd.
 *
 *   MODE 0  exactly the above -- what ships today.
 *   MODE 1  the above plus an explicit fseek(fp, 0, SEEK_END) after the open.
 *
 * Every line is EXACTLY 64 characters plus a newline and carries its writer
 * id, its sequence number, and 44 bytes of filler unique to that writer, so
 * a torn write, an overwrite or a lost line is detectable from the host with
 * no guesswork.
 *
 * Usage:  logtest <path> <id 0-9> <count> <mode 0|1>
 *
 * Why this test exists: libnix13's fstat() returns st_size 0 while reporting
 * success (fixed in v10.24, see amiga/fstat.c). A C library that cannot
 * measure a file's end is a candidate for one that cannot append to it, and
 * the log has been tearing at 5-way concurrency with the lock demonstrably
 * held. This settles which layer is at fault.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <proto/exec.h>

#define SEM_NAME "AmiBinkdLogTest"
#define LINE_LEN 64

static struct SignalSemaphore *test_sem (void)
{
    static struct SignalSemaphore *cached = NULL;
    struct SignalSemaphore *sem;

    if (cached != NULL)
        return cached;

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

    cached = sem;
    return sem;
}

int main (int argc, char **argv)
{
    const char *path;
    int id, count, mode, seq, i;
    char line[LINE_LEN + 2];
    char filler[45];
    struct SignalSemaphore *sem;
    int opened = 0, failed = 0;

    if (argc < 5)
    {
        printf ("usage: logtest <path> <id 0-9> <count> <mode 0|1>\n");
        return 20;
    }
    path  = argv[1];
    id    = atoi (argv[2]);
    if (id < 0 || id > 9) { printf ("id must be 0-9\n"); return 20; }
    count = atoi (argv[3]);
    mode  = atoi (argv[4]);

    /* 44 bytes of filler, a different character per writer */
    memset (filler, 'A' + (id % 26), 44);
    filler[44] = '\0';

    sem = test_sem ();
    if (sem == NULL)
    {
        printf ("logtest %d: could not get semaphore\n", id);
        return 20;
    }

    for (seq = 0; seq < count; ++seq)
    {
        FILE *fp = NULL;

        ObtainSemaphore (sem);

        for (i = 0; fp == NULL && i < 10; ++i)
            fp = fopen (path, "a");

        if (fp)
        {
            if (mode == 1)
                fseek (fp, 0, SEEK_END);      /* THE CANDIDATE FIX */

            /* exactly LINE_LEN chars, then \n */
            snprintf (line, sizeof line, "LINE id=%02d seq=%04d %s",
                      id, seq % 10000, filler);
            fprintf (fp, "%s\n", line);
            fclose (fp);
            opened++;
        }
        else
            failed++;

        ReleaseSemaphore (sem);
    }

    printf ("logtest id=%d mode=%d wrote=%d failed=%d linelen=%d\n",
            id, mode, opened, failed, (int) strlen (line));
    return 0;
}
