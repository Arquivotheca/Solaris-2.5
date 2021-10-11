#
#ident	"@(#)Makefile	1.6	94/12/05 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= id
XPG4PROG= id

include ../Makefile.cmd

.KEEP_STATE:

LDLIBS +=	-lintl
CFLAGS +=	-v
$(XPG4) := CFLAGS += -DXPG4

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
