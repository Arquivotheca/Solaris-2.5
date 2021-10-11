#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= ps

include ../Makefile.cmd

LDLIBS += -lw -lintl
XGETFLAGS += -a -x ps.xcl

FILEMODE= 04555
OWNER= root
GROUP= sys

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
