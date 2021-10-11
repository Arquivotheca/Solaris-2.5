#
#ident	"@(#)Makefile	1.12	95/07/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		cachefs
LIBPROG=	mount
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

PROGOBJS=	mount.o fslib.o
COMMONOBJS=	$(CACHEFSDIR)/subr.o

include		../Makefile.cachefs

CPPFLAGS += -I../.. -o $@

fslib.o: ../../fslib.c
	$(COMPILE.c) ../../fslib.c
