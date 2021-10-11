#
#ident	"@(#)Makefile	1.4	94/09/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/strings/Makefile

PROG= strings

include ../Makefile.cmd

LDLIBS += -lelf -lintl

CFLAGS += -v

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
