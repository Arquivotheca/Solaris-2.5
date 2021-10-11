#
#ident	"@(#)Makefile	1.6	94/12/05 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= tabs

include ../Makefile.cmd

LDLIBS += -lcurses -lintl
CFLAGS += -v

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
