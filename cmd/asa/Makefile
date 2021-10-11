#
#ident	"@(#)Makefile	1.1	95/06/05 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
#

PROG= asa

include ../Makefile.cmd

LDLIBS		+=	-lintl -lw
CFLAGS		+=	-v

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
