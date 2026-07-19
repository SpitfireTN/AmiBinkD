/* amiga_glue.h - AmigaOS 3.x bsdsocket.library glue (no ixemul/ixnet) */

#ifndef _AMIGA_GLUE_H
#define _AMIGA_GLUE_H

#include <proto/bsdsocket.h>

int amiga_socket_init(void);
void amiga_socket_cleanup(void);

#endif
