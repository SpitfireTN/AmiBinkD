/* amiga_glue.h - AmigaOS 3.x bsdsocket.library glue (no ixemul/ixnet) */

#ifndef _AMIGA_GLUE_H
#define _AMIGA_GLUE_H

/* v10.12: redirect every bsdsocket.library call site reached through
 * this header (client.c, server.c, protocol.c, iptools.c, rfc2553.c -
 * anything that gets here via iphdr.h) to a per-Task-aware lookup
 * instead of the bare shared SocketBase global. Must come before the
 * <proto/bsdsocket.h> include right below - see amiga_glue.c for the
 * matching duplicate needed there, since that file's own direct
 * bsdsocket includes happen before it reaches this header. Full
 * rationale on amiga_current_socketbase() itself, in amiga_glue.c. */
extern struct Library *amiga_current_socketbase (void);
#define BSDSOCKET_BASE_NAME amiga_current_socketbase()

#include <proto/bsdsocket.h>

int amiga_socket_init(void);
void amiga_socket_cleanup(void);
/* v10.23: point the CURRENT SocketBase at our errno. Per-SocketBase
 * setting, so it must be called for the shared base and for every
 * private per-child base. Without it, TCPERR() reports stale values. */
void amiga_set_errno_ptr(void);
struct Library *amiga_open_private_socketbase(void);
void amiga_close_private_socketbase(struct Library *lib);

#endif
