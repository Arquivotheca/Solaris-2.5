#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
#pragma	ident	"@(#)Makefile	1.4	93/07/22 SMI"
#
# lib/libkstat/Makefile
#

LIBRARY= libkstat.a
VERS= .1
OBJECTS= kstat.o

# include library definitions
include ../Makefile.lib

ROOTLIBDIR= $(ROOT)/usr/lib
LIBS = $(DYNLIB) # $(LINTLIB) maybe

# definitions for install_h target
HDRS=		kstat.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

# definitions for lint
CLEANFILES += $(LINTOUT)

.KEEP_STATE:

all: $(LIBS)

install: all $(ROOTLIBS) $(ROOTLINKS)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

lint: $(LINTLIB)

# include library targets
include ../Makefile.targ
