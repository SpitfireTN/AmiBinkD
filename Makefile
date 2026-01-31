###############################################################################
# AmiBinkD Makefile (Modern Auto‑Build Version)
# Target: AmigaOS 3.x (GCC), no ixemul, bsdsocket.library
###############################################################################

CC      = gcc
CFLAGS  = -O2 -Wall -DAMIGA -DPROTOTYPES=1
LIBS    = -lbsdsocket

###############################################################################
# Source Files
###############################################################################

SRCS =  binkd.c protocol.c client.c server.c \
        ftnaddr.c ftnnode.c ftnq.c inbound.c readcfg.c readflo.c \
        tools.c binlog.c crypt.c md5c.c md5b.c bsy.c \
        iptools.c rfc2553.c srv_gai.c snprintf.c xalloc.c compress.c \
        exitproc.c breaksig.c run.c \
        main.c net.c ftn.c log.c amiga_glue.c

OBJS = $(SRCS:.c=.o)

###############################################################################
# Build Rules
###############################################################################

all: amibinkd

amibinkd: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

# Generic .c → .o rule
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

###############################################################################
# Cleanup
###############################################################################

clean:
	rm -f *.o amibinkd

###############################################################################
# End of Makefile
###############################################################################
