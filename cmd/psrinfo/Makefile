#
# ident	"@(#)Makefile	1.2	95/08/22 SMI"
#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
# cmd/psrinfo/Makefile
#

PROG=	psrinfo

include	../Makefile.cmd

LDLIBS += -lkstat
OWNER = root
GROUP = sys

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

clean:

lint: lint_SRCS

include	../Makefile.targ
