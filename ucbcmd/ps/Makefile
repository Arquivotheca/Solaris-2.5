#
#ident	"@(#)Makefile	1.4	92/12/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= ps

include ../Makefile.ucbcmd

LDLIBS +=  -lw -lelf

FILEMODE=	4755
OWNER= root
GROUP= sys

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.ucbtarg
