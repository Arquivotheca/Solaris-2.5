#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.11	94/09/16 SMI"
#
# lib/libauth/Makefile
#
LIBRARY= libauth.a
VERS = .1

OBJECTS= ia_framework.o

# include library definitions
include ../Makefile.lib

# library dependency
LDLIBS += -lcmd

LIBS += $(DYNLIB)


#SCHEMEDIR=../scheme/unix_scheme

#include $(SCHEMEDIR)/Makefile.objs

#SCH_OBJS=$(DEFOBJS:%=$(SCHEMEDIR)/%)
#SCH_PICS =$(DEFPICS:%=$(SCHEMEDIR)/%)
OBJS += $(SCH_OBJS)
PICS += $(SCH_PICS)



.KEEP_STATE:

all:  $(LIBS)

install: all $(ROOTLIBS) $(ROOTLINKS)

lint: llib-lauth.ln

# include library targets
include ../Makefile.targ

#additional dependencies

$(LIBRARY) : $(OBJS)
$(DYNLIB) : $(PICS)

#defobjs defpics : FRC
#	@cd $(SCHEMEDIR); pwd; $(MAKE) $@
#	@pwd

FRC:
