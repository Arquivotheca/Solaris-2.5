#
# @(#)Makefile 1.2 93/11/05 SMI
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#

PROG= setfacl

include ../Makefile.cmd

LDLIBS += -lsec -lintl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
