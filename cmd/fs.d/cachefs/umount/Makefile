#
#ident	"@(#)Makefile	1.3	95/07/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		cachefs
LIBPROG=	umount
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

PROGOBJS=	umount.o fslib.o
#COMMONOBJS=	$(CACHEFSDIR)/subr.o

include		../Makefile.cachefs

CPPFLAGS += -I../.. -o $@

fslib.o: ../../fslib.c 
	$(COMPILE.c) ../../fslib.c
