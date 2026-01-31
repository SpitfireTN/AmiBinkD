CC      = gcc
CFLAGS  = -O2 -Wall -DAMIGA -DPROTOTYPES=1
LIBS    = -lbsdsocket

OBJS =  binkd.o protocol.o client.o server.o \
        ftnaddr.o ftnnode.o ftnq.o inbound.o readcfg.o readflo.o \
        tools.o binlog.o crypt.o md5c.o md5b.o bsy.o \
        iptools.o rfc2553.o srv_gai.o snprintf.o xalloc.o compress.o \
        exitproc.o breaksig.o run.o \
        main.o net.o ftn.o log.o amiga_glue.o

amibinkd: $(OBJS)
	$(CC) -o amibinkd $(OBJS) $(LIBS)

clean:
	rm -f *.o amibinkd
