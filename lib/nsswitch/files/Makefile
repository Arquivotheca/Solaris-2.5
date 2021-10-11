#
#ident	"@(#)Makefile	1.11	95/02/15 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/nsswitch/files/Makefile
#
LIBRARY= libnss_files.a
VERS= .1

OBJECTS= bootparams_getbyname.o ether_addr.o getgrent.o gethostent.o \
	getnetent.o getprotoent.o getpwnam.o getrpcent.o getservent.o \
	getspent.o files_common.o

# include library definitions, do not change order of include and DYNLIB
include ../../Makefile.lib

DYNLIB=	nss_files.so$(VERS)

# See below.
PSLIB= $(DYNLIB).ps
ROOTETCLIB= $(ROOT)/etc/lib
ROOTPSLIB=  $(ROOTETCLIB)/$(DYNLIB)
CLOBBERFILES += $(PSLIB)

$(ROOTPSLIB) :=	FILEMODE= 755

$(ROOTETCLIB)/%: %.ps
	$(INS.rename)

LINTFLAGS=
CPPFLAGS += -D_REENTRANT
LDLIBS += -lc

LIBS += $(DYNLIB)

$(DYNLIB) := DYNFLAGS += -zdefs

.KEEP_STATE:

all: $(LIBS) $(PSLIB)

# Generate a library version nss_files.so$(VERS).ps that does not have
# a declared dependency on libc.  The ".ps" is stripped off the name
# before it is installed on the root partition in /etc/lib.  This is
# needed for diskless clients and for hosts that mount /usr over NFS.
#
$(PSLIB): pics .WAIT $$(PICS)
	$(LD) -o $@ -dy -G $(DYNFLAGS) $(PICS)
	$(POST_PROCESS_SO)

# note conditional assignment below
install: all $(ROOTLIBS) $(ROOTPSLIB)

# include library targets
include ../../Makefile.targ

# to avoid make warnings, this conditional assignment must
# follow the include of Makefile.targ
$(ROOTPSLIB) := ROOTLIBDIR=$(ROOTETCLIB)
