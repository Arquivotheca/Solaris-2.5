#
#ident	"@(#)Makefile	1.2	92/06/23 SMI"
#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
# cmd/fs.d/ufs/fsirand/Makefile

FSTYPE=		ufs
LIBPROG=	fsirand

OTHERINSTALL=	$(ROOTUSRSBIN)/$(LIBPROG)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(LIBPROG)

include		../../Makefile.fstype

lint:		lint_PROG

$(ROOTUSRSBIN)/$(LIBPROG):
		-$(RM) $@; $(SYMLINK) $(LINKVALUE) $@
