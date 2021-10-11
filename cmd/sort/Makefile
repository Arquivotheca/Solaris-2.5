#
#ident	"@(#)Makefile	1.10	94/12/06 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= sort
XPG4PROG= sort

include ../Makefile.cmd

.KEEP_STATE:

CFLAGS += -v
$(XPG4):= CPPFLAGS += -DXPG4
LDLIBS += -lmapmalloc -lw -lintl
XGETFLAGS += -a -x sort.xcl


all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
