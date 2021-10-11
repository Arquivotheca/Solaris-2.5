#
#ident	"@(#)Makefile	1.13	95/01/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= grep 

include ../Makefile.cmd

LDLIBS += -lgen -lintl
XGETFLAGS += -a -x grep.xcl


.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
