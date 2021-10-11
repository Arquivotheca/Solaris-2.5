#
#ident	"@(#)Makefile	1.7	92/08/10 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		ufs
LIBPROG=	df
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

LDLIBS += -lintl

# for messaging catalogue file
#
POFILE= df.po
XGETFLAGS += -a -x df.xcl

catalog:        $(POFILE)

lint:		lint_PROG
