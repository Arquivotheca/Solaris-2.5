#
#ident	"@(#)Makefile	1.2	90/04/19 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libnls/Makefile
#
LIBRARY= libnls.a

OBJECTS= nlsdata.o	nlsenv.o	nlsrequest.o

# include library definitions
include ../Makefile.lib

.KEEP_STATE:

all: $(TXTS) $(LIBS)

install: all $(ROOTLIBS)

# include library targets
include ../Makefile.targ
