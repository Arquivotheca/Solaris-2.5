#
#ident	"@(#)Makefile	1.6	92/08/10 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		ufs
LIBPROG=	fstyp
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

# for messaging catalog
#
POFILE= fstyp.po

catalog:        $(POFILE)


LDLIBS += -lintl

lint:		lint_PROG
