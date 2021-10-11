#
#ident	"@(#)Makefile	1.6	93/02/12 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		cachefs
LIBPROG=	fsck
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

PROGOBJS=	fsck.o res.o
COMMONOBJS=	$(CACHEFSDIR)/subr.o

include		../Makefile.cachefs
