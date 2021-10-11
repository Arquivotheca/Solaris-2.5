#
#ident	"@(#)Makefile	1.6	92/07/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		ufs
LIBPROG=	tunefs
ATTMK=		$(LIBPROG)

OTHERINSTALL=	$(ROOTUSRSBIN)/$(LIBPROG)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(LIBPROG)

include		../../Makefile.fstype

LDLIBS	+=	-ladm

lint:		lint_PROG

$(ROOTUSRSBIN)/$(LIBPROG):	FRC
		-$(RM) $@; $(SYMLINK) $(LINKVALUE) $@

FRC:

