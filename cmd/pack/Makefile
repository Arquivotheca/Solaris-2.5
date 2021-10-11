#
#ident	"@(#)Makefile	1.7	94/11/17 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= pack

include ../Makefile.cmd

LDLIBS += -lintl
CFLAGS += -v
XGETFLAGS += -a -x $(PROG).xcl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
