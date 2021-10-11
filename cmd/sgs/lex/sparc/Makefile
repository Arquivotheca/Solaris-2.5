#
#ident	"@(#)Makefile	1.5	93/11/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/lex/sparc/Makefile
#

PROG=		lex
LIBRARY=	libl.a
INCLIST_sparc=	-I../common

include 	../../../Makefile.cmd
include 	../../../../lib/Makefile.lib
include		../Makefile.com

.KEEP_STATE:

all:		$(PROG)

all_lib:	$(LIBS)

install:	all $(ROOTCCSBINPROG) $(ROOTFORMS)

install_lib:	$(ROOTLIBS)

include		../Makefile.targ
include		../../../Makefile.targ
