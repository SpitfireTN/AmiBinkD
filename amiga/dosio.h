/* See amiga/dosio.c -- writes that bypass libnix's unlocked fd table. */
#ifndef AMIGA_DOSIO_H
#define AMIGA_DOSIO_H
int amiga_dos_append (const char *path, const char *data, int len);
#endif
