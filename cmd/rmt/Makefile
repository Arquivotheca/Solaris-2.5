#
#ident	"@(#)Makefile	1.2	92/07/14 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/rmt/Makefile
#

PROG= 		rmt

include ../Makefile.cmd

LDLIBS += -lintl
CFLAGS += -Xa

RELUSRSBIN=	../usr/sbin
ROOTSYMLINK=	$(ROOTETC)/$(PROG)

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	-$(RM) $@; $(SYMLINK) $(RELUSRSBIN)/$(PROG) $@

clean:

lint:	lint_PROG

include ../Makefile.targ
