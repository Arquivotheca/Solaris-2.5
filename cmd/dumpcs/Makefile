#
#ident	"@(#)Makefile	1.1	93/03/23 SMI"
#
# Copyright (c) 1993, Sun Microsystems, Inc.
#

PROG= dumpcs

include ../Makefile.cmd

LDLIBS +=  -lintl -lw

.KEEP_STATE:

all: $(PROG)

install : all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
