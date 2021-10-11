#
#ident	"@(#)Makefile	1.8	95/03/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= printf

include ../Makefile.cmd

LDLIBS += -lintl -lgen

CFLAGS += -v -Xa

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
