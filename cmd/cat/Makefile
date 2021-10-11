#
#ident	"@(#)Makefile	1.8	94/08/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= cat

include ../Makefile.cmd

LDLIBS		+=	-lintl -lw
CPPFLAGS	+=	-DEUC
CFLAGS		+=	-v

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
