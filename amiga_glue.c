/* amiga_glue.c - AmigaOS 3.x bsdsocket.library glue */

#include <exec/types.h>
#include <exec/libraries.h>

#include <proto/exec.h>
#include <proto/bsdsocket.h>

#include <stdio.h>

struct Library *SocketBase = NULL;

int amiga_socket_init(void)
{
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (SocketBase == NULL)
    {
        printf("Unable to open bsdsocket.library\n");
        return -1;
    }

    return 0;
}

void amiga_socket_cleanup(void)
{
    if (SocketBase != NULL)
    {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}
