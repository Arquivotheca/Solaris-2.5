#
#ident	"@(#)Makefile	1.10	95/03/21 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/cal/Makefile

PROG= cal 

include ../Makefile.cmd

SED= sed
DCFILE= $(PROG).dc

LDLIBS += -lintl
CFLAGS += -v

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

$(DCFILE): $(PROG).c
	$(RM) $(DCFILE)
	$(COMPILE.cpp) $(PROG).c  | \
	$(XGETTEXT) $(XGETFLAGS) -t -
	$(SED) -e '/^domain/d' messages.po > $@
	$(RM) messages.po

include ../Makefile.targ
