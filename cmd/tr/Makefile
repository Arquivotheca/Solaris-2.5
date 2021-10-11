#
#ident	"@(#)Makefile	1.8	95/09/08 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= tr
XPG4PROG= tr

include ../Makefile.cmd

.KEEP_STATE:

LDLIBS += -lintl -lw
CFLAGS += -v
$(XPG4):= CPPFLAGS += -DXPG4
CPPFLAGS += -I../../lib/libc/port/gen -I../../lib/libc/inc

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
