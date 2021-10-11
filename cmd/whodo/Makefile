#
#ident	"@(#)Makefile	1.8	94/12/20 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/whodo/Makefile

PROG= whodo

include ../Makefile.cmd

SED= sed
DCFILE= $(PROG).dc

LDLIBS += -lintl
CFLAGS += -v

FILEMODE = 04555
OWNER = root

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTUSRSBINPROG) $(ROOTETCPROG)

$(ROOTETCPROG):
	-$(RM) $@
	-$(SYMLINK) ../usr/sbin/$(PROG) $@

clean:

lint:	lint_PROG

$(DCFILE):
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) -t $(PROG).c
	$(SED) -e '/^domain/d' messages.po > $@
	$(RM) messages.po

include ../Makefile.targ
