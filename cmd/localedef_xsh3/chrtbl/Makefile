#
#ident	"@(#)Makefile	1.16	93/09/01 SMI"
#
# Copyright (c) 1989-93 by Sun Microsystems, Inc.
#
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#
# cmd/localedef/chrtbl/Makefile

include ../../Makefile.cmd
include ../Makefile.locale

PROG=	chrtbl
# remove references to NPROG

CTYPE_FILE=	$(BUILDDIRS2:%=ctype.%)
NUMERIC=	$(BUILDDIRS:%=LC_NUMERIC.%)

ROOTLINK=	$(ROOTBIN)/wchrtbl
ROOTCTYPEDIR=	$(BUILDDIRS2:%=$(ROOTLOCALE)/%/LC_CTYPE)
ROOTCTYPE=	$(ROOTCTYPEDIR:%=%/ctype)
ROOTNUMERICDIR=	$(BUILDDIRS:%=$(ROOTLOCALE)/%/LC_NUMERIC)
ROOTNUMERIC=	$(ROOTNUMERICDIR:%=%/numeric)


ROOTDIRS +=	$(ROOTISO) $(ROOTCTYPEDIR) $(ROOTNUMERICDIR)

# LC_NUMERIC.iso_8859_1 is built but not installed.
CLOBBERFILES=	$(CTYPE_FILE) $(NUMERIC) LC_CTYPE LC_NUMERIC \
		LC_NUMERIC.iso_8859_1 

$(ROOTISO)/LC_CTYPE := DIRMODE= $(TMPDIRMODE)

.KEEP_STATE:
.NO_PARALLEL:

all: $(PROG) $(CTYPE_FILE) $(NUMERIC)

install: all $(ROOTPROG) $(ROOTLINK) $(ROOTDIRS) $(ROOTSYMLN) \
	$(ROOTCTYPE) $(ROOTNUMERIC)

ctype.% + LC_NUMERIC.% : $(PROG) chrtbl.%.src
	$(RM) ctype.$* LC_NUMERIC.$*
	./$(PROG) $<
	$(MV) LC_CTYPE ctype.$*
	$(MV) LC_NUMERIC LC_NUMERIC.$*

$(ROOTLOCALE)/%/LC_CTYPE/ctype: ctype.%
	$(INS.rename)

$(ROOTLOCALE)/%/LC_NUMERIC/numeric: LC_NUMERIC.%
	$(INS.rename)

$(ROOTLINK): $(ROOTPROG)
	$(RM) $@; $(LN) $(ROOTPROG) $@

lint: lint_PROG

clean:

include ../Makefile.target
include ../../Makefile.targ
