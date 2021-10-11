#
#ident	"@(#)Makefile	1.5	94/10/11 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= mesg

include ../Makefile.cmd

LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
