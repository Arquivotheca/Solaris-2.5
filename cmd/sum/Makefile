#
#ident	"@(#)Makefile	1.6	94/09/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= sum

include ../Makefile.cmd
LDLIBS += -lintl
CFLAGS += -v

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
