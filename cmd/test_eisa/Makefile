#
#ident "@(#)Makefile	1.1	93/02/24 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= test_eisa

include ../Makefile.cmd

LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) $(PROG).mk

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
