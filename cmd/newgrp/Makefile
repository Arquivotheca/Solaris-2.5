#
#ident	"@(#)Makefile	1.8	93/11/12 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= newgrp

include ../Makefile.cmd

LDLIBS += -lcrypt_i -lintl
FILEMODE = 04755
OWNER = root
GROUP = sys
XGETFLAGS += -a -x newgrp.xcl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
