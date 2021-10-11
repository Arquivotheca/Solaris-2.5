#
#ident	"@(#)Makefile	1.35	95/03/03 SMI"
#
#	Copyright (c) 1994 by Sun Microsystems, Inc.
#

# Object lists are organized into primary (most frequently used code) and
# secondary lists (less frequently used code, ie. a.out support).

P_COMOBJS=	analyze.o	elf.o		globals.o	malloc.o \
		mutex.o		paths.o		setup.o		util.o \
		dlfcns.o

S_COMOBJS=	debug.o		profile.o	object.o	a.out.o \
		cache.o

P_MACHOBJS=	sparc_elf.o	_setup.o	_globals.o

S_MACHOBJS=	sparc_a.out.o

P_ASOBJS=	boot.o		boot_elf.o	caller.o	zero.o

S_ASOBJS=	boot_a.out.o	note.o

G_OBJS=		reloc.o

CRTI=		pics/crti.o
CRTN=		pics/crtn.o
CRTS=		$(CRTI)		$(CRTN)

include		../Makefile.com

# Add any a.out and machine specific flags.
CPPFLAGS +=	-DA_OUT
ASFLAGS +=	-K pic
LINTFLAGS +=	-DA_OUT

.KEEP_STATE:

all:		$(RTLD)

install:	all $(ROOTDYNLIB) $(ETCDYNLIB)

include		../Makefile.targ
