#
#ident	"@(#)Makefile	1.4	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= bfs

include ../Makefile.cmd

LDLIBS += -lgen -lw

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
