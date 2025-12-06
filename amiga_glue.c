#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <stdio.h>

struct Library *SocketBase = NULL;

int amiga_socket_init(void) {
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        printf("Unable to open bsdsocket.library\n");
        return -1;
    }
    return 0;
}

void amiga_socket_cleanup(void) {
    if (SocketBase) CloseLibrary(SocketBase);
}
