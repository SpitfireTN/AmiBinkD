// net.h
int net_connect(const char *host, int port);

// net.c
#include "net.h"
#include <proto/bsdsocket.h>
#include <string.h>

extern struct Library *SocketBase;

int net_connect(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    // TODO: resolve host, connect
    return sock;
}

