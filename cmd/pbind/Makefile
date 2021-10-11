#
# ident	"@(#)Makefile	1.1	92/09/25 SMI"
#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
# cmd/pbind/Makefile
#

PROG=	pbind

include	../Makefile.cmd

LDLIBS += -lintl

OWNER = root
GROUP = sys

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

clean:

lint: lint_SRCS

include	../Makefile.targ
