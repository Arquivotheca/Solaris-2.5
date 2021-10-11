#
#ident	"@(#)Makefile	1.6	94/02/01 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libgen/Makefile
#
LIBRARY= libgen.a

OBJECTS= \
basename.o     bgets.o        bufsplit.o     copylist.o     dirname.o  \
eaccess.o      gmatch.o       isencrypt.o    mkdirp.o       p2open.o  \
pathfind.o     reg_compile.o  reg_step.o     regcmp.o       regex.o  \
rmdirp.o       strccpy.o      strecpy.o      strfind.o      strrspn.o  \
strtrns.o


# include library definitions
include ../Makefile.lib

ROOTLIBDIR=	$(ROOT)/usr/ccs/lib
# definitions for install_h target
HDRS=		regexpr.h libgen.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

CPPFLAGS= -K pic -DPIC -D_REENTRANT -Dsparc $(RTLDINC) $(CPPFLAGS.master)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(TXTS) $(LIBS)

install: $(ROOTLIBS)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

# include library targets
include ../Makefile.targ
