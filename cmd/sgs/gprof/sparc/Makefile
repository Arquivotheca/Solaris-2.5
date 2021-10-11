#
#ident	"@(#)Makefile	1.11	94/08/09 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/gprof/sparc/Makefile
#


PROG=		gprof

include 	../Makefile.com

DEFLIST +=	-DSPARC

.KEEP_STATE:

all:		$(PROG)

install:	all .WAIT $(ROOTCCSBINPROG) $(ROOTCCSBLURB)

include		../Makefile.targ
