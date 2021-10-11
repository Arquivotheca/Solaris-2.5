#
# ident	"@(#)Makefile	1.8	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/secure/Makefile
#

LIBRARY	=	liblpsec.a

OBJECTS =	secure.o


include		../../../../lib/Makefile.lib
include         ../../Makefile.lp

CPPFLAGS =	-I../../include $(CPPFLAGS.master)

.KEEP_STATE:
# for messaging catalog
#
POFILE= secure.po
SRCS= $(OBJECTS:%.o=%.c)
XGETFLAGS=-a -x $(LPROOT)/lp.xcl

catalog:        $(POFILE)

all install :	$(LIBS)

strip lint :

include		../../../../lib/Makefile.targ
