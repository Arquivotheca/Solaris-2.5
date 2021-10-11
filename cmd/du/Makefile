#
#ident	"@(#)Makefile	1.7	94/09/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= du
XPG4PROG= du

include ../Makefile.cmd

LDLIBS += -lintl
CFLAGS += -v
$(XPG4) := CFLAGS += -DXPG4

.KEEP_STATE:

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
