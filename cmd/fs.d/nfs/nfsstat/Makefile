#
#ident	"@(#)Makefile	1.5	92/09/16 SMI"	/* SVr4.0 1.12	*/
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/nfs/nfsstat/Makefile

PROG=		nfsstat

include		../../../Makefile.cmd

LDLIBS +=		-lkstat -lkvm -lelf

FILEMODE=       02555
GROUP=          sys

.KEEP_STATE:

all:		$(PROG)

install:	all $(ROOTPROG)

lint:	lint_PROG

clean:     

include ../../../Makefile.targ
