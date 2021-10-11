#
#ident	"@(#)Makefile	1.10	94/12/07 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= uniq

include ../Makefile.cmd
LDLIBS += -lintl -lw
CFLAGS += -v


XGETFLAGS += -a -x uniq.xcl
.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
