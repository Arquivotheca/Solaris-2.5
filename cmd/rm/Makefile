#
#ident	"@(#)Makefile	1.8	95/04/10 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= rm
XPG4PROG= rm

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
