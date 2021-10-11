#
#ident	"@(#)Makefile	1.6	92/08/26 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/ufs/edquota/Makefile

FSTYPE=		ufs
LIBPROG=	edquota
ATTMK=		$(LIBPROG)

OTHERINSTALL=	$(ROOTUSRSBIN)/$(LIBPROG)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(LIBPROG)

include		../../Makefile.fstype

lint:		lint_PROG

$(ROOTUSRSBIN)/$(LIBPROG):
		-$(RM) $@; $(SYMLINK) $(LINKVALUE) $@
