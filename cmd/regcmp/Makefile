#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= regcmp 

include ../Makefile.cmd

LDLIBS += -lgen -lintl

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTCCSBINPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
