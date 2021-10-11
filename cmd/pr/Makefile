#
#ident	"@(#)Makefile	1.9	94/09/02 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= pr
XPG4PROG= pr

include ../Makefile.cmd

LDLIBS		+=	-lintl
CPPFLAGS	+=	-DEUC
$(XPG4):= CPPFLAGS += -DXPG4
XGETFLAGS += -a -x pr.xcl

.KEEP_STATE:

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
