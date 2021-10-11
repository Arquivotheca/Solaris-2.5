#
#ident "@(#)Makefile	1.1	92/09/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= diskscan

include ../Makefile.cmd

LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) $(PROG).mk

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
