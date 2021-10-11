#
#ident	"@(#)Makefile	1.6	93/02/17 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libeti/panel/Makefile
#
LIBRARY=	libpanel.a

OBJECTS=  \
bottom.o        move.o          replace.o       update.o \
delete.o        misc.o          new.o           top.o

# include library definitions
include ../../Makefile.lib

ROOTLIBDIR= $(ROOT)/usr/ccs/lib

# TXTS allows the AT&T makefile to be bootstrapped into the NSE.
TXTS=

# definitions for lint target
LIBS += $(LINTLIB)
LINTSRC= $(LINTLIB:%.ln=%)
ROOTLINT= $(LINTSRC:%=$(ROOTLIBDIR)/%)

# definitions for install_h target
HDRS=		panel.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(TXTS) $(LIBS)

llib-lpanel: llib-lpanel.c
	    $(RM) $@
	    cp llib-lpanel.c $@

$(LINTLIB):= SRCS=llib-lpanel
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=

install: all $(ROOTLIBS) $(ROOTLINT)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

# include library targets
include ../../Makefile.targ
