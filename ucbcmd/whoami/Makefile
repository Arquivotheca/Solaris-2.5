#
#ident	"@(#)Makefile	1.3	94/11/08 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= whoami

include ../Makefile.ucbcmd

LDLIBS += -lgen -lintl
CFLAGS += -v
 
XGETFLAGS += -a -x rmdir.xcl


FILEMODE=	755

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.ucbtarg
