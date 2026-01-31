/* breaksig.c – AmigaOS 3.x break handling (no POSIX signals) */

#include <exec/types.h>
#include <exec/tasks.h>
#include <proto/exec.h>

#include <stdlib.h>
#include <stdio.h>

#include "sys.h"
#include "common.h"
#include "tools.h"
#include "sem.h"

/*
 * AmigaOS does NOT support POSIX signals.
 * Instead, we watch for SIGBREAKF_CTRL_C (Ctrl-C) or other break bits.
 */

#ifndef SIGBREAKF_CTRL_C
#define SIGBREAKF_CTRL_C (1L << 12)
#endif

extern int binkd_exit;

/* Called when a break is detected */
static void amiga_break(void)
{
    Log(1, "Break signal received.");
    binkd_exit = 1;
}

/*
 * Install exit handler and prepare break handling.
 * On AmigaOS this does NOT install signal() handlers.
 */
int set_break_handlers(void)
{
    atexit(exitfunc);

    /* Nothing else to install — Amiga break handling is polled */

    return 1;
}

/*
 * Called periodically from the main loop to check for Ctrl-C.
 */
void check_break(void)
{
    ULONG sigs;

    /* Check if any break bits are pending */
    sigs = SetSignal(0L, 0L);

    if (sigs & SIGBREAKF_CTRL_C)
    {
        amiga_break();
    }
}
