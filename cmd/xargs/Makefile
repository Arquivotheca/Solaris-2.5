#
#ident	"@(#)Makefile	1.6	95/07/01 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= xargs

include ../Makefile.cmd

LDLIBS += -lintl -lgen -lw

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
