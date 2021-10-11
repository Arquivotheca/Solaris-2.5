#
#ident	"@(#)Makefile	1.1	94/11/10 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ptools/ptree/Makefile
#

PROG = ptree

include ../../Makefile.cmd
include ../Makefile.com

FILEMODE = 04555
OWNER = root
GROUP = bin

# LDLIBS +=	../libproc/libproc.a

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROCBINPROG)

clean:

lint:   lint_PROG

include ../../Makefile.targ
