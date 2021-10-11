#
#ident	"@(#)Makefile	1.4	93/02/05 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#

PROG= kgmon

include ../Makefile.cmd

LDLIBS += -lkvm -lelf

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
