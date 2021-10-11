#
# ident	"@(#)Makefile	1.14	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/lpNet/Makefile
#

LIBRARY	=	liblpNet.a

XDROBJS =	xdrMsgs.o	\
		_xdrMsgs.o

OBJECTS =	networkMgmt.o	\
		_networkMgmt.o	\
		errorMgmt.o	\
		logMgmt.o	\
		mpipes.o	\
		lists.o		\
		memdup.o	\
		$(XDROBJS)

LPINC =		../../include

XDRSRCS =	$(XDROBJS:%.o=%.c)
XDRHDRS =	$(XDROBJS:%.o=$(LPINC)/%.h)

# CLEANFILES =	$(XDRSRCS)

RPCGEN	=	rpcgen -DRPCGEN

include		../../../../lib/Makefile.lib
include         ../../Makefile.lp

CPPFLAGS =	-I$(LPINC) $(CPPFLAGS.master)

TXTS =		$(XDROBJS:.o=.x)

.KEEP_STATE:
# for messaging catalog
#
POFILE= lpNet.po
SRCS= $(OBJECTS:%.o=%.c)
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl

catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM)	$@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS)        $(POFILE).i
	sed "/^domain/d"        messages.po     > $@
	$(RM) messages.po $(POFILE).i



all install :	$(TXTS) $(LIBS)

strip lint :

include		../../../../lib/Makefile.targ

#$(XDROBJS) :	$$(@:%.o=$(LPINC)/%.h)

#$(XDRSRC) :	$$(@:%.c=%.x)
#		$(RM) $@
#		echo "#""ident\t\"@(#)lp:lib/lpNet/Makefile	1.10\"" > $@
#		$(RPCGEN) -c $(@:%.c=%.x)  >> $@

#$(XDRHDRS) :	$$(@:$(LPINC)/%.h=%.x)
#		$(RM) $@
#		echo "#""ident\t\"@(#)lp:lib/lpNet/Makefile	1.10\"" > $@
#		$(RPCGEN) -h $(@:%.h=%.x)  >> $@
