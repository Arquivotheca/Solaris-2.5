#
# ident	"@(#)Makefile	1.8	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/bsd/Makefile
#

LIBRARY =	liblpbsd.a

OBJECTS	=	displayq.o	\
		escape.o	\
		fatalmsg.o	\
		findfld.o	\
		gethostnm.o	\
		global.o	\
		log.o		\
		parse.o		\
		psfile.o	\
		rmjob.o		\
		rsendjob.o	\
		sndrcv.o


include		../../../../lib/Makefile.lib
include         ../../Makefile.lp

CPPFLAGS =	-I../../include $(CPPFLAGS.master)
# fo rmessaging catalog
#
POFILE= bsd.po
SRCS= $(OBJECTS:%.o=%.c)
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl
 
.KEEP_STATE:
  
catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	  $(RM)	$@
	  $(COMPILE.cpp)   $(SRCS) > $(POFILE).i
	  $(XGETTEXT) $(XGETFLAGS)        $(POFILE).i
	  sed "/^domain/d"        messages.po > $@
	  $(RM) messages.po $(POFILE).i

all install :	$(LIBS)

strip lint :

include		../../../../lib/Makefile.targ
