#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= col

include ../Makefile.cmd

.KEEP_STATE:

LDLIBS += -lw -lintl

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
