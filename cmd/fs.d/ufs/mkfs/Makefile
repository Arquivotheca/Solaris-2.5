#
#ident	"@(#)Makefile	1.9	92/08/10 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		ufs
LIBPROG=	mkfs
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

# for messaging catalog
#
POFILE= mkfs.po

catalog:        $(POFILE)


LDLIBS += -lintl -lm -ladm

lint:		lint_PROG
