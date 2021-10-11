#
#pragma ident	"@(#)Makefile	1.6	93/09/16 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/devlinks/libdevfs/Makefile
#
LIBRARY=	libdevinfo.a

OBJECTS=	devfswalk.o devfssubr.o

# include library definitions
include ../Makefile.lib

# definitions for install_h target
HDRS=		libdevinfo.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

CPPFLAGS +=	-v

LINTOUT=	lint.out
CLEANFILES=	$(LINTOUT) $(LINTLIB)

.KEEP_STATE:

all: 		$(LIBS)

install: all $(ROOTLIBS)

install_h: $(ROOTHDRS)

check: $(CHECKHDRS)

lint:		$(LINTLIB)

# include library targets
include ../Makefile.targ
