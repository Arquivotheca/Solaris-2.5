#
#ident	"@(#)Makefile	1.8	94/12/20 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/w/Makefile

PROG= w 

include ../Makefile.cmd

SED= sed
DCFILE= $(PROG).dc

LDLIBS += -lkstat -lintl
CFLAGS += -v

ROOTLINK= $(ROOTBIN)/uptime

FILEMODE=	4555
OWNER=		root		# w needs to read /proc entries
GROUP=		bin

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTBIN) $(ROOTPROG) $(ROOTLINK)

$(ROOTLINK):
	$(RM) $@
	$(LN) $(ROOTPROG) $@

clean:

lint:	lint_PROG

$(DCFILE):
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) -t $(PROG).c
	$(SED) -e '/^domain/d' messages.po > $@
	$(RM) messages.po

include ../Makefile.targ
