#
#ident	"@(#)Makefile	1.7	94/09/09 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= fold

include ../Makefile.cmd

CFLAGS  += -v
LDLIBS	+= -lintl -lw

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
