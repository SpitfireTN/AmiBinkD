// log.c — Reign of Fire BBS Group FTN Engine Logging
// Amiga‑Optimized Logging Backend
// Continuation of AmiBinkd v9.02 (AmiBinkd PRO/FREEWARE, AmiCron/AmiTask Edition)
// Originally coded 2011–2014 by Rudi Timmermans

#include "log.h"
#include <proto/dos.h>
#include <string.h>
#include <time.h>

static BPTR fh = 0;

void log_open(const char *path) {
    fh = Open(path, MODE_NEWFILE);

    if (fh) {
        /* Timestamp */
        time_t now = time(NULL);
        struct tm *tmv = localtime(&now);
        char ts[64];
        if (tmv) {
            sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d",
                tmv->tm_year + 1900,
                tmv->tm_mon + 1,
                tmv->tm_mday,
                tmv->tm_hour,
                tmv->tm_min,
                tmv->tm_sec
            );
        } else {
            strcpy(ts, "Unknown-Time");
        }

        /* Historical Lineage */
        const char *history =
            "AmiBinkd v9.02 — AmiBinkd PRO/FREEWARE (AmiCron/AmiTask Edition)\n"
            "Originally coded 2011–2014 by Rudi Timmermans\n"
            "------------------------------------------------------------\n"
            "This build continues the AmiBinkd legacy under the\n"
            "Reign of Fire BBS Group modernization project.\n"
            "\n";

        /* ROF Banner */
        const char *banner =
            "============================================================\n"
            "   ***  REIGN OF FIRE BBS GROUP — FTN ENGINE ONLINE  ***\n"
            "------------------------------------------------------------\n"
            "   Sysop: SpitfireTN / Gary McCulloch\n"
            "   Network Hub • Amiga‑Powered • Est. 2024\n"
            "   https://rofbbs.com\n"
            "============================================================\n"
            "\n";

        /* Version Tag */
        const char *version =
            "ROF FTN Engine Build: 10.0 (AmiBinkd Legacy Continuation)\n"
            "Log Started: ";

        /* Write everything */
        Write(fh, banner, strlen(banner));
        Write(fh, history, strlen(history));
        Write(fh, version, strlen(version));
        Write(fh, ts, strlen(ts));
        Write(fh, "\n\n", 2);
    }
}

void log_write(const char *msg) {
    if (fh) Write(fh, msg, strlen(msg));
}

void log_close(void) {
    if (fh) Close(fh);
}
