#
#ident	"@(#)Makefile	1.6	94/11/18 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= tail
XPG4PROG= tail

include ../Makefile.cmd
LDLIBS += -lintl
$(XPG4) := CFLAGS += -DXPG4

.KEEP_STATE:

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
