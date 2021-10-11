#
#ident	"@(#)Makefile	1.8	93/05/20 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libintl/Makefile
#
LIBRARY= libintl.a
VERS = .1

OBJECTS= gettext.o wdresolve.o

# include library definitions
include ../Makefile.lib

# library dependency; wdresolve.c needs it.
LDLIBS += -lw

LINTFLAGS=

LIBS += $(DYNLIB)
CPPFLAGS += -D_REENTRANT

# definitions for install_h target
HDRS=		libintl.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

# set mode on installed header at same precedence as the
# conditionally assigned mode for the installed dynamic library.
# this avoids rebuilds caused by reinstallation of the header
# with alternating file modes.
$(ROOTHDRS) :=	FILEMODE= 644

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)


.KEEP_STATE:

all: $(LIBS) 

install: all $(ROOTLIBS) $(ROOTLINKS)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

# include library targets
include ../Makefile.targ
