/* probe -- where does a libnix13 program die on this system?
 *
 * logtest produced NO console output and created NO file, so it stops before
 * its first printf AND before its first fopen. This narrows that down.
 *
 * Each stage is recorded TWICE: once through AmigaDOS Open/Write (which does
 * not touch the C runtime's stdio at all) and once through printf. If the DOS
 * trail advances but the console stays silent, stdio is the broken part; if
 * neither advances, the program is dying before main().
 *
 * Usage:  DH4:probe
 */

#include <string.h>
#include <stdio.h>
#include <exec/types.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define TRAIL "DH4:probe-trail.txt"

static void stage (const char *msg)
{
    BPTR f = Open ((STRPTR) TRAIL, MODE_READWRITE);
    if (f)
    {
        Seek (f, 0, OFFSET_END);
        Write (f, (APTR) msg, (LONG) strlen (msg));
        Close (f);
    }
}

int main (int argc, char **argv)
{
    struct SignalSemaphore *sem;
    FILE *fp;

    stage ("A entered main\n");
    printf ("A entered main\n"); fflush (stdout);

    stage ("B printf survived\n");

    Forbid ();
    sem = (struct SignalSemaphore *) FindSemaphore ((STRPTR) "AmiBinkdProbe");
    Permit ();
    stage ("C FindSemaphore ok\n");
    printf ("C FindSemaphore ok\n"); fflush (stdout);

    if (sem == NULL)
    {
        Forbid ();
        sem = (struct SignalSemaphore *)
              AllocMem (sizeof (struct SignalSemaphore), MEMF_PUBLIC | MEMF_CLEAR);
        if (sem)
        {
            InitSemaphore (sem);
            sem->ss_Link.ln_Name = (char *) "AmiBinkdProbe";
            AddSemaphore (sem);
        }
        Permit ();
    }
    stage ("D semaphore created\n");
    printf ("D semaphore created\n"); fflush (stdout);

    ObtainSemaphore (sem);
    stage ("E obtained\n");

    fp = fopen ("DH4:probe-out.txt", "a");
    stage (fp ? "F fopen ok\n" : "F fopen FAILED\n");
    if (fp)
    {
        fprintf (fp, "hello from probe\n");
        fclose (fp);
        stage ("G wrote and closed\n");
    }
    ReleaseSemaphore (sem);

    stage ("H done\n");
    printf ("H done -- all stages passed\n"); fflush (stdout);
    return 0;
}
