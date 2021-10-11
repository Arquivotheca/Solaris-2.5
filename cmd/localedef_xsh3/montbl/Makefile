#
#ident	"@(#)Makefile	1.12	93/09/01 SMI"
#
# Copyright (c) 1990-93 by Sun Microsystems, Inc.
#
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#
# cmd/localedef/montbl/Makefile

include ../../Makefile.cmd
include ../Makefile.locale

ROOTMONTBLDIR=	$(BUILDDIRS:%=$(ROOTLOCALE)/%/LC_MONETARY)
MONTBL=		$(BUILDDIRS:%=LC_MONETARY.%)
ROOTMONTBL=	$(ROOTMONTBLDIR:%=%/monetary)

PROG=	montbl

ROOTDIRS +=	$(ROOTMONTBLDIR)

CLOBBERFILES=	$(MONTBL)

.KEEP_STATE:
.NO_PARALLEL:

all: $(PROG) $(MONTBL)

install: all $(ROOTPROG) $(ROOTLOCALE) $(ROOTDIRS) $(ROOTSYMLN) $(ROOTMONTBL)

LC_MONETARY.%: montbl.%.src
	$(RM) $@
	./$(PROG) $<
	$(MV) LC_MONETARY $@

$(ROOTLOCALE)/%/LC_MONETARY/monetary: LC_MONETARY.%
	$(INS.rename)

lint: lint_PROG

clean:

include ../Makefile.target
include ../../Makefile.targ
