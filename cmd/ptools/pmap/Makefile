#
#ident	"@(#)Makefile	1.2	95/03/01 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ptools/pmap/Makefile
#

PROG = pmap

include ../../Makefile.cmd
include ../Makefile.com

FILEMODE = 0555
OWNER = bin
GROUP = bin

CPPFLAGS +=	-I../libproc
LDLIBS +=	../libproc/libproc.a

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROCBINPROG)

clean:

lint:   lint_PROG

include ../../Makefile.targ
