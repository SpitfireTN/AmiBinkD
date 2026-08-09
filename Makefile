###############################################################################
# AmiBinkD Makefile - AmigaOS 3.x native build, no ixemul/ixnet
#
# Uses bsdsocket.library directly (amiga_glue.c) and the modern bebbo
# m68k-amigaos-gcc cross-compiler with the nix13 C runtime (NOT ixemul).
# Concurrency: AmigaOS has no fork()/pthreads without ixemul, so this build
# runs binkp sessions synchronously, one at a time (see branch.c) - same
# approach the upstream DOS build uses.
###############################################################################

TOOLCHAIN = /home/spitfiretn/tools/amiga-gcc-toolchain/bin
CC        = $(TOOLCHAIN)/m68k-amigaos-gcc

DEFINES = -DAMIGA -DHAVE_STDARG_H -DHAVE_SNPRINTF -DHAVE_VSNPRINTF -DHAVE_INTMAX_T -DHAVE_SOCKLEN_T \
          -DHAVE_UNISTD_H -DHAVE_SYS_TIME_H -DHAVE_SYS_PARAM_H -DHAVE_SYS_IOCTL_H \
          -DOS="\"Amiga\"" -DHTTPS -DAMIGADOS_4D_OUTBOUND

# -msoft-float: force pure software floating point everywhere, never emit
# real 68881/68882-style hardware FPU (F-line) instructions. Without this,
# GCC's implicit default CPU/FPU target can emit F-line opcodes the 68040's
# onboard FPU doesn't support (it's missing some transcendental
# instructions vs. real 68881/882 without the FPSP software package) -
# confirmed the hard way: Guru 8000000B (Line-1111 / unimplemented FPU
# instruction) on first real-hardware run, crashing before any console
# output. Removing our own double/float usage earlier just happened to
# dodge a *linking* symptom (missing __muldf3) - it never actually forced
# software float for the rest of the (much larger) untouched upstream code.
CFLAGS  = -mcrt=nix13 -msoft-float $(DEFINES) -Wall -O2
LDFLAGS = -mcrt=nix13 -msoft-float
LIBS    =

###############################################################################
# Source Files
###############################################################################

SRCS =  binkd.c tools.c ftnaddr.c ftndom.c ftnnode.c ftnq.c \
        client.c server.c protocol.c bsy.c inbound.c breaksig.c branch.c \
        readcfg.c readflo.c prothlp.c iptools.c rfc2553.c run.c binlog.c \
        exitproc.c getw.c xalloc.c setpttl.c https.c md5b.c crypt.c \
        compress.c srif.c pmatch.c getopt.c \
        amiga_glue.c amiga/rename.c amiga/getfree.c amiga/sem.c amiga/touch.c amiga/delete.c amiga/msleep.c
# srv_gai.c deliberately excluded - its srv_getaddrinfo() is only for
# platforms with a real resolver (HAVE_RESOLV_H/WITH_FTS5004), which
# AmigaOS has neither of. srv_gai.h's own macro fallback
# (srv_getaddrinfo -> getaddrinfo) covers every caller instead.
# snprintf.c deliberately excluded too - libnix's own stdio.h already
# provides a real snprintf/vsnprintf (see HAVE_SNPRINTF/HAVE_VSNPRINTF
# below), and upstream's fallback implementation needs long double
# arithmetic (pow10/round) that this toolchain's libgcc can't link
# (missing double-precision soft-float routines - a real toolchain gap,
# not something worth working around for code we don't even need).

OBJS = $(SRCS:.c=.o)

###############################################################################
# Build Rules
###############################################################################

all: amibinkd

amibinkd: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

###############################################################################
# Cleanup
###############################################################################

clean:
	rm -f *.o amiga/*.o amibinkd

.PHONY: all clean
