#
#ident	"@(#)Makefile	1.2	94/12/19 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/liblm/Makefile
#
LIBRARY= liblm.a
VERS=.1

OBJECTS= lm_shutdown.o

# include library definitions
include ../Makefile.lib

LIBS = $(DYNLIB)

LINTFLAGS=	-u
LINTOUT=	lint.out
CLEANFILES=	$(LINTOUT) $(LINTLIB)

LDLIBS +=       -lelf

.KEEP_STATE:

all: $(LIBS) $(TXTS)

install: all $(ROOTLIBS) $(ROOTLINKS)

lint:	$(LINTLIB)

# include library targets
include ../Makefile.targ
