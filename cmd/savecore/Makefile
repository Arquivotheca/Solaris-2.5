#
#ident	"@(#)Makefile	1.5	93/07/21 SMI"
#
# Copyright (c) 1989-1993, by Sun Microsystems, Inc.
#
# cmd/savecore/Makefile
#

PROG= savecore

include ../Makefile.cmd

LDLIBS += -lkvm -lelf
CFLAGS += -v

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
