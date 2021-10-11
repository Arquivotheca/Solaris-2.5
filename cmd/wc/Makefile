#
#ident	"@(#)Makefile	1.6	94/10/11 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= wc

include ../Makefile.cmd

LDLIBS += -lintl -lw

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
