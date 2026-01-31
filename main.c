#include "net.h"
#include "ftn.h"
#include "log.h"
#include "amiga_glue.h"

/*
 * AmiBinkd v9.10 — ROF Continuation Edition
 * Based on AmiBinkd v9.02 (AmiBinkd PRO/FREEWARE, AmiCron/AmiTask Edition)
 * Originally coded 2011–2014 by Rudi Timmermans
 * Modernized & Continued by Reign of Fire BBS Group
 */

int main(void) {

    /* Initialize Amiga TCP/IP stack */
    if (amiga_socket_init() < 0)
        return 1;

    /* Open log and write startup banner */
    log_open("S:amibinkd.log");
    log_write("AmiBinkd v9.10 starting...\n");
    log_write("Reign of Fire BBS Group — FTN Engine Online\n");

    /* Main FTN session loop */
    ftn_loop();   /* Placeholder for future BinkP/FTN engine */

    /* Shutdown footer */
    log_write("AmiBinkd v9.10 shutting down...\n");
    log_close();

    /* Cleanup TCP/IP */
    amiga_socket_cleanup();

    return 0;
}
