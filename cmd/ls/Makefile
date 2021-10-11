#
#ident	"@(#)Makefile	1.10	94/12/20 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/ls/Makefile

PROG= ls
XPG4PROG= ls

include ../Makefile.cmd

SED= sed
DCFILE= $(PROG).dc

LDLIBS += -lgen -lw -lintl
CFLAGS += -v
$(XPG4) := CFLAGS += -DXPG4

.KEEP_STATE:

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

lint:	lint_PROG

$(DCFILE):
	$(RM) messages.po
	$(COMPILE.cpp) $(PROG).c | \
	$(XGETTEXT) $(XGETFLAGS) -t -
	$(SED) -e '/^domain/d' messages.po > $@
	$(RM) messages.po

include ../Makefile.targ
