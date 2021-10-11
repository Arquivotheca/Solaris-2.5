#
#ident	"@(#)Makefile	1.1	94/11/10 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ptools/prun/Makefile
#

PROG = prun

include ../../Makefile.cmd
include ../Makefile.com

FILEMODE = 0555
OWNER = bin
GROUP = bin

# LDLIBS +=	../libproc/libproc.a

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROCBINPROG)

clean:

lint:   lint_PROG

include ../../Makefile.targ
