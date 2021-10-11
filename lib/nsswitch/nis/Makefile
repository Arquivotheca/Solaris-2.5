#
#ident	"@(#)Makefile	1.8	93/04/12 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/nsswitch/nis/Makefile
#
LIBRARY= libnss_nis.a
VERS= .1

OBJECTS= nis_common.o \
	getgrent.o getpwnam.o getspent.o \
	gethostent.o getrpcent.o \
	getservent.o getnetent.o getprotoent.o \
	bootparams_getbyname.o ether_addr.o \
	getnetgrent.o

# include library definitions, do not change the order of include and DYNLIB
include ../../Makefile.lib

DYNLIB= nss_nis.so$(VERS)

LINTFLAGS=
CPPFLAGS += -D_REENTRANT
LDLIBS += -lnsl -lsocket -lc

LIBS += $(DYNLIB)

.KEEP_STATE:

all: $(LIBS)

install: all $(ROOTLIBS)

# include library targets
include ../../Makefile.targ
