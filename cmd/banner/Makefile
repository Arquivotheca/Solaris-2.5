#
#ident	"@(#)Makefile	1.5	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= banner 

include ../Makefile.cmd

LDLIBS	+= -lintl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
