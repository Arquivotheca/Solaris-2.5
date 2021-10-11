#
#ident	"@(#)Makefile	1.6	92/08/26 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/ufs/labelit/Makefile

FSTYPE=		ufs
LIBPROG=	labelit
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

LDLIBS += -lm

lint :=		PROG=$(LIBPROG)

lint:		lint_PROG
