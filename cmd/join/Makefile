#
#ident	"@(#)Makefile	1.8	94/10/12 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= join

include ../Makefile.cmd

LDLIBS += -lintl -lw
CFLAGS += -v
XGETFLAGS += -a -x join.xcl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
