#
#ident	"@(#)Makefile	1.7	94/10/18 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= pg

include ../Makefile.cmd

CFLAGS += -DSINGLE -v
LDLIBS += -lgen -lw -lcurses -lintl
XGETFLAGS += -a -x $(PROG).xcl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
