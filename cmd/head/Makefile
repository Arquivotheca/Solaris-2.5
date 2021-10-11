#
#ident	"@(#)Makefile	1.5	94/10/05 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= head

include ../Makefile.cmd
LDLIBS += -lintl
CFLAGS += -v


.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
