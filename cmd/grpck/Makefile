#
#ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/grpck/Makefile
#

PROG= grpck

include ../Makefile.cmd

RELUSRSBIN=	../usr/sbin
ROOTSYMLINK=	$(ROOTETC)/$(PROG)

LDLIBS	+= -lintl

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	-$(RM) $@; $(SYMLINK) $(RELUSRSBIN)/$(PROG) $@

clean:

lint:	lint_PROG

include ../Makefile.targ
