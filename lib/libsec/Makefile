#
#ident	@(#)Makefile 1.2 94/07/13 SMI
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libsec/Makefile
#
LIBRARY= libsec.a
VERS = .1

OBJECTS= \
aclcheck.o	aclsort.o	aclmode.o	acltext.o


# include library definitions
include ../Makefile.lib

LIBS += $(DYNLIB)

ROOTLIBDIR=	$(ROOT)/usr/lib
# definitions for install_h target
HDRS=		
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(TXTS) $(LIBS)

install: $(ROOTLIBS) $(ROOTLINKS)

install_h: $(ROOTHDRS)

# include library targets
include ../Makefile.targ
