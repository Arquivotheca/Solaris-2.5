#
#ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= paste

include ../Makefile.cmd

LDLIBS += -lw -lintl
XGETFLAGS += -a -x $(PROG).xcl
.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
