#
#ident	"@(#)Makefile	1.10	93/07/06 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/nsswitch/nisplus/Makefile
#
LIBRARY= libnss_nisplus.a
VERS= .1

OBJECTS= nisplus_common.o \
		gethostent.o getrpcent.o \
		getnetent.o getprotoent.o getservent.o \
		getpwnam.o getgrent.o getspent.o \
		bootparams_getbyname.o ether_addr.o \
		getnetgrent.o

# include library definitions, do not change order of include and DYNLIB
include ../../Makefile.lib

DYNLIB=	nss_nisplus.so$(VERS)

LINTFLAGS=
CPPFLAGS += -D_REENTRANT
LDLIBS += -lnsl -lsocket -lc

LIBS += $(DYNLIB)

.KEEP_STATE:

all: $(LIBS)

install: all $(ROOTLIBS)

# include library targets
include ../../Makefile.targ
