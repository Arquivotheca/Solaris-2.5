#
#ident	"@(#)Makefile	1.3	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= gettext

include ../Makefile.cmd
LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
