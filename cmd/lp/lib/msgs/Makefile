#
# ident	"@(#)Makefile	1.8	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/msgs/Makefile
#

LIBRARY	=	liblpmsg.a

OBJECTS =	_getmessage.o	\
		_putmessage.o	\
		getmessage.o	\
		hslconv.o	\
		putmessage.o	\
		msgfmts.o	\
		mclose.o	\
		mconnect.o	\
		mdisconnect.o	\
		mgetputm.o	\
		mlisten.o	\
		mcreate.o	\
		mdestroy.o	\
		mneeds.o	\
		mopen.o		\
		mread.o		\
		mrecv.o		\
		msend.o		\
		mwrite.o	\
		streamio.o	\
		fifo_buffs.o	\
		read_fifo.o	\
		write_fifo.o


include		../../../../lib/Makefile.lib
include         ../../Makefile.lp


CPPFLAGS =	-I../../include $(CPPFLAGS.master)

.KEEP_STATE:
# for messaging catalog
#
POFILE= msgs.po
SRCS= $(OBJECTS:%.o=%.c)
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl

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
