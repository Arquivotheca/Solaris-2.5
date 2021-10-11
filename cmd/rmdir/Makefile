#
#ident	"@(#)Makefile	1.7	94/12/06 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= rmdir

include ../Makefile.cmd

LDLIBS += -lgen -lintl
CFLAGS += -v

XGETFLAGS += -a -x rmdir.xcl

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
