#
#ident	"@(#)Makefile	1.7	92/08/26 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/ufs/quot/Makefile

FSTYPE=		ufs
LIBPROG=	quot
ATTMK=		$(LIBPROG)

OTHERINSTALL=	$(ROOTUSRSBIN)/$(LIBPROG)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(LIBPROG)

include		../../Makefile.fstype

LDLIBS	+=	-ladm

lint:		lint_PROG

$(ROOTUSRSBIN)/$(LIBPROG):
		-$(RM) $@; $(SYMLINK) $(LINKVALUE) $@
