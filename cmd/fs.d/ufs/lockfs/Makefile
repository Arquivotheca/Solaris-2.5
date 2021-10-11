#
#ident	"@(#)Makefile	1.4	92/08/26 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# cmd/fs.d/ufs/lockfs/Makefile

FSTYPE=		ufs
LIBPROG=	lockfs
ATTMK=		$(LIBPROG)

OTHERINSTALL=	$(ROOTUSRSBIN)/$(LIBPROG)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(LIBPROG)

include		../../Makefile.fstype

LDLIBS += -lc2

lint:		lint_PROG

$(ROOTUSRSBIN)/$(LIBPROG):
	$(RM) $@; $(SYMLINK) $(LINKVALUE) $@
