#
#ident	"@(#)Makefile	1.2	93/01/27 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#

PROG= des

include ../Makefile.cmd

LDLIBS += -lcrypt_d

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ

