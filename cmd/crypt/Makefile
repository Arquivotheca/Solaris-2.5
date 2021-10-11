#
#ident	"@(#)Makefile	1.5	93/01/27 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= crypt

include ../Makefile.cmd

LDLIBS += -lcrypt_i

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ

