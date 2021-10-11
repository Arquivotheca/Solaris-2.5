#
#ident	"@(#)Makefile	1.7	93/02/17 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libeti/menu/Makefile
#
LIBRARY=	libmenu.a

OBJECTS=  \
affect.o       itemopts.o     menufore.o     menuserptr.o   scale.o \
chk.o          itemusrptr.o   menuformat.o   menusub.o      show.o \
connect.o      itemvalue.o    menugrey.o     menuwin.o      terminit.o \
curitem.o      link.o         menuitems.o    newitem.o      topitem.o \
driver.o       menumark.o     newmenu.o      visible.o \
global.o       menuback.o     menuopts.o     pattern.o \
itemcount.o    menucursor.o   menupad.o      post.o

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
HDRS=		menu.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(TXTS) $(LIBS)

llib-lmenu: llib-lmenu.c
	    $(RM) $@
	    cp llib-lmenu.c $@

$(LINTLIB):= SRCS=llib-lmenu
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=

install: all $(ROOTLIBS) $(ROOTLINT)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

# include library targets
include ../../Makefile.targ
