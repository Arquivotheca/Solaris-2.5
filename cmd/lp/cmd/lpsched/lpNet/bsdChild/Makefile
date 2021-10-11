#
# ident	"@(#)Makefile	1.5	92/09/03 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/lpsched/lpNet/bsdChild/Makefile
#

include		../../../../Makefile.lp

LIBRARY =	bsdChild.a

OBJECTS =	bsdChild.o	\
		cancel.o	\
		getstatus.o	\
		netutil.o	\
		printjob.o	\
		printreq.o	\
		recvjob.o

include		../../../../../../lib/Makefile.lib

LPNETINC =	../include
CPPFLAGS =	-I$(LPNETINC) -I$(LPINC) $(DEBUG) $(CPPFLAGS.master)

#
# for messaging catalog
POFILE= bsdChild.po
SRCS=   $(OBJECTS:%.o=%.c)
XGETFLAGS += -a -x $(LPROOT)/lp.xcl


.KEEP_STATE:

all install :	$(TXTS) $(LIBRARY)

catalog:        $(POFILE)
 
 $(POFILE):      $(SRCS)
	 $(RM)	$@
	 $(COMPILE.cpp)   $(SRCS) > $(POFILE).i
	 $(XGETTEXT)     $(XGETFLAGS)    $(POFILE).i
	 sed "/^domain/d"        messages.po     > $@
	 $(RM) messages.po       $(POFILE).i


strip lint :

include		../../../../../../lib/Makefile.targ
