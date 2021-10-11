#
# ident	"@(#)Makefile	1.9	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/access/Makefile
#

LIBRARY	=	liblpacc.a

OBJECTS =	allowed.o	\
		bang.o		\
		change.o	\
		dumpaccess.o	\
		files.o		\
		loadaccess.o

include		../../../../lib/Makefile.lib
include		../../Makefile.lp

CPPFLAGS =	-I../../include $(CPPFLAGS.master)


# for messaging catalog file
#
POFILE= access.po
POFILES= $(OBJECTS:%.o=%.po)
XGETFLAGS += -a -x $(LPROOT)/lp.xcl

.KEEP_STATE :

all install :	$(LIBS)

catalog:        $(POFILE)
 
$(POFILE):      $(POFILES)
	 $(RM)	$@
	 cat     $(POFILES)      > $@

strip lint :

include		../../../../lib/Makefile.targ
