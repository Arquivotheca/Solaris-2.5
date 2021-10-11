#
#ident	"@(#)Makefile	1.1	94/12/05 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#

PROG= pathchk

include ../Makefile.cmd

LDLIBS		+=	-lintl -lw
CFLAGS		+=	-v

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
