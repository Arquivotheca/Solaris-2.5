#
#ident	"@(#)Makefile	1.7	94/09/19 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= tee

include ../Makefile.cmd
LDLIBS += -lintl
CFLAGS += -v

#
# For messaging catalogue file
XGETFLAGS += -a -x tee.xcl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
