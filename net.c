#include "net.h"
#include <proto/bsdsocket.h>
#include <string.h>
#include <stdlib.h>

extern struct Library *SocketBase;

int net_connect(const char *host, int port) {
    int sock;
    struct hostent *he;
    struct sockaddr_in sin;

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    /* Resolve hostname */
    he = gethostbyname(host);
    if (!he) {
        CloseSocket(sock);
        return -1;
    }

    /* Build sockaddr */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port);
    memcpy(&sin.sin_addr, he->h_addr, he->h_length);

    /* Connect */
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        CloseSocket(sock);
        return -1;
    }

    return sock;
}
