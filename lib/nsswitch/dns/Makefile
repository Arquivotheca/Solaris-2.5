#
#ident	"@(#)Makefile	1.5	93/04/12 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/nsswitch/dns/Makefile
#
LIBRARY= libnss_dns.a
VERS= .1

OBJECTS= gethostent.o

# include library definitions, do not change order of include and DYNLIB
include ../../Makefile.lib

DYNLIB=	nss_dns.so$(VERS)

LINTFLAGS=
LDLIBS += -lresolv -lc
CPPFLAGS += -D_REENTRANT -DSYSV

LIBS += $(DYNLIB)

.KEEP_STATE:

all: $(LIBS)

install: $(ROOTLIBS)

# include library targets
include ../../Makefile.targ
