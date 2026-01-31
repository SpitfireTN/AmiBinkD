/*
 * ftn.c — AmiBinkD FTN session loop
 * Reign of Fire BBS branding edition
 */

#include "ftn.h"
#include "log.h"

void ftn_loop(void)
{
    /* ------------------------------------------------------------ */
    /*  Reign Of Fire BBS — FTN Engine Banner                       */
    /* ------------------------------------------------------------ */

    log_write("\n");
    log_write("============================================================\n");
    log_write("   ***  REIGN OF FIRE BBS GROUP — FTN ENGINE ONLINE  ***\n");
    log_write("------------------------------------------------------------\n");
    log_write("   Sysop: SpitfireTN / Gary McCulloch\n");
    log_write("   Network Hub • Amiga‑Powered • Est. 2024\n");
    log_write("   https://rofbbs.com\n");
    log_write("============================================================\n");
    log_write("\n");

    /* Placeholder loop message */
    log_write("FTN loop running...\n");

    /* TODO: implement BinkP protocol here */
}
