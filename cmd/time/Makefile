#
#ident	"@(#)Makefile	1.6	94/08/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= time

include ../Makefile.cmd

LDLIBS += -lintl
CFLAGS += -v

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
