#include "net.h"
#include "ftn.h"
#include "log.h"
#include "amiga_glue.h"

int main(void) {
    if (amiga_socket_init() < 0) return 1;

    log_open("S:amibinkd.log");
    log_write("Amibinkd starting...\n");

    ftn_loop();   // placeholder for FTN session loop

    log_write("Amibinkd shutting down...\n");
    log_close();

    amiga_socket_cleanup();
    return 0;
}
