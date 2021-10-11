#
# ident	"@(#)Makefile	1.8	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/filters/Makefile
#

LIBRARY	=	liblpflt.a

OBJECTS =	conv.o		\
		delfilter.o	\
		dumpfilters.o	\
		freefilter.o	\
		getfilter.o	\
		filtertable.o	\
		insfilter.o	\
		loadfilters.o	\
		putfilter.o	\
		regex.o		\
		search.o	\
		trash.o

include         ../../Makefile.lp

include		../../../../lib/Makefile.lib

CPPFLAGS =	-I../../include $(CPPFLAGS.master)

.KEEP_STATE:
# for messaging catalog
#
POFILE= filters.po
SRCS= $(OBJECTS:%.o=%.c)
XGETFLAGS=-a -x $(LPROOT)/lp.xcl
 
catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	 $(RM)	$@
	 $(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	 $(XGETTEXT) $(XGETFLAGS)        $(POFILE).i
	 sed "/^domain/d"        messages.po     > $@
	 $(RM) messages.po $(POFILE).i

all install :	$(LIBS)

strip lint :

include		../../../../lib/Makefile.targ
