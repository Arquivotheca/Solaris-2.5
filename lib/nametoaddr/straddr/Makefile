#
#ident	"@(#)Makefile	1.13	93/04/12 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/nametoaddr/straddr
#
LIBRARY= libstraddr.a
VERS= .2

OBJECTS= straddr.o

# include library definitions
include ../../Makefile.lib

# set exclusively to avoid libtcpip.so being built up.
# do not change ordering of includes and DYNLIB
DYNLIB= straddr.so$(VERS)
LIBLINKS= straddr.so

LINTFLAGS=
CPPFLAGS += -I../inc -D_REENTRANT
LDLIBS += -lnsl -lc

LIBS += $(DYNLIB)

.KEEP_STATE:

all: $(LIBS)

install: $(ROOTLIBS) $(ROOTLINKS)

# include library targets
include ../../Makefile.targ
