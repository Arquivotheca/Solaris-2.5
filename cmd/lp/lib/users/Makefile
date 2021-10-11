#
# ident	"@(#)Makefile	1.8	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/users/Makefile
#

LIBRARY	=	liblpusr.a

OBJECTS =	usermgmt.o	\
		loadpri.o	\
		storepri.o


include		../../../../lib/Makefile.lib
include         ../../Makefile.lp

CPPFLAGS =	-I../../include $(CPPFLAGS.master)

.KEEP_STATE:

# for messaging catalog
#
POFILE= users.po
SRCS= $(OBJECTS:%.o=%.c)
XGETFLAGS += -a -x $(LPROOT)/lp.xcl

all install :	$(LIBS)

strip lint :

catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM)	$@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS)        $(POFILE).i
	sed "/^domain/d"        messages.po     > $@
	$(RM) messages.po $(POFILE).i

include		../../../../lib/Makefile.targ
