#
#ident	"@(#)Makefile 1.1	94/11/09 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
LIBRARY= libdoor.a
VERS = .1
INCLIST= -Iinc

# include common library definitions
include ../Makefile.lib

CPPFLAGS= $(INCLIST) $(CPPFLAGS.master)
LIBS = $(DYNLIB)

ROOTHDRDIR = $(ROOT)/usr/include/sys
ROOTHDRS = $(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS = $(HDRS:%.h=%.check)

$(ROOTHDRS) :=	FILEMODE = 644

.KEEP_STATE:

all: $(MACHLIBS)

install: $(ROOTLIBS) $(ROOTLINKS)

$(ROOTHDRDIR)/% : %
	$(INS.file)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

clean clobber: FRC
	@cd $(MACH); pwd; $(MAKE) $@

links:
	-$(RM) $(MACH)
	$(SYMLINK) `mach` $(MACH)
include ../Makefile.mach

FRC:
