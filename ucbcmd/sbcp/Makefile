#
#ident	"@(#)Makefile	1.19	95/08/29 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# ucbcmd/sbcp/Makefile
#

PROG=		sbcp

include ../Makefile.ucbcmd

CLEANFILES=	sysent.s
POFILE=
 
FILEMODE=	755

.KEEP_STATE:

all:	$(PROG)

install: all $(ROOT4LIBPROG)

clean:
	$(RM) $(OBJS) $(CLEANFILES)
 
# Following is a kludge to use private version of ld with necessary fixes.
# This can (and should) be removed when Solaris 2.3 becomes the CBE (Common
# Build Environment).

LD=../../cmd/sgs/proto/sparc/ld
LDOPTS=LD_LIBRARY_PATH=../../cmd/sgs/libld/sparc:../../cmd/sgs/liblddbg/sparc

LIBBC=	../../lib/libbc/sparc/libc.so.1.9
LIBUCB=	../../ucblib/libucb/sparc/libucb.so.1
# need libc/sparc/inc for PIC.h
ASFLAGS=-P -D_ASM -DPIC -K pic -I../../lib/libc/sparc/inc
LDFLAGS=-R /usr/4lib:/usr/ucblib -e _start -G -t -z text -I /usr/lib/ld.so.1
OBJS=	sbcp.o
LIBS=	-lmapmalloc $(LIBBC) $(LIBUCB) -lsocket -lnsl -lelf -laio -lc

$(PROG):	$(OBJS)
	$(LDOPTS) $(LD) $(LDFLAGS) -o sbcp $(OBJS) $(LIBS)
	$(POST_PROCESS)

$(OBJS):	sysent.s

sysent.s:	syscalls.list mksysent
	-$(RM) sysent.s
	sh ./mksysent > sysent.s

include ../Makefile.ucbtarg
