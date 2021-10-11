#
#ident	"@(#)Makefile	1.6	93/06/10 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/swap/Makefile

PROG= swap

include ../Makefile.cmd

LDLIBS += -lelf

FILEMODE= 02555
GROUP= sys

ROOTSYMLINK= $(ROOTETCPROG)

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTUSRSBINPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	$(RM) $@; $(SYMLINK) ../usr/sbin/$(PROG) $@

clean:

lint:	lint_PROG

include ../Makefile.targ
