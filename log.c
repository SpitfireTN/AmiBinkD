// log.h
void log_open(const char *path);
void log_write(const char *msg);
void log_close(void);

// log.c
#include "log.h"
#include <proto/dos.h>

static BPTR fh = 0;

void log_open(const char *path) {
    fh = Open(path, MODE_NEWFILE);
}

void log_write(const char *msg) {
    if (fh) Write(fh, msg, strlen(msg));
}

void log_close(void) {
    if (fh) Close(fh);
}
