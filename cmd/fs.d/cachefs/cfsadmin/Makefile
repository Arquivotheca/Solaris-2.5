#
#ident	"@(#)Makefile	1.15	93/05/18 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		cachefs
LIBPROG=	cfsadmin
ATTMK=		$(LIBPROG)

OTHERINSTALL=   $(ROOTUSRSBIN)/$(LIBPROG)
LINKVALUE=      ../lib/fs/$(FSTYPE)/$(LIBPROG)

include		../../Makefile.fstype

PROGOBJS=	cfsadmin.o
COMMONOBJS=	$(CACHEFSDIR)/subr.o

include		../Makefile.cachefs

$(ROOTUSRSBIN)/$(LIBPROG):
		-$(RM) $@; $(SYMLINK) $(LINKVALUE) $@

