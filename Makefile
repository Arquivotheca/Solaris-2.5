#
#ident	"@(#)Makefile	1.54	95/07/17 SMI"
#
# Copyright (c) 1989-1995, by Sun Microsystems, Inc.
# All rights reserved.
#
# Makefile for system source
#
# include global definitions
include Makefile.master
#
# the Targetdirs file is the AT&T target.dirs file in a makefile format.
# it defines TARGETDIRS and ROOTDIRS.
include Targetdirs

COMMON_SUBDIRS=	uts lib stand psm uts/adb cmd devices ucblib ucbcmd

SUBDIRS= $(COMMON_SUBDIRS) $($(MACH)_SUBDIRS)

HDRSUBDIRS=	uts head lib cmd ucbhead

# UCB headers are bug-for-bug compatable and not checkable against the header
# standards.
#
CHKHDRSUBDIRS=	head uts lib cmd

MSGSUBDIRS=     cmd     ucbcmd     lib
DOMAINS= \
	SUNW_OST_ADMIN \
	SUNW_OST_NETRPC \
	SUNW_OST_OSCMD \
	SUNW_OST_OSLIB \
	SUNW_OST_UCBCMD

MSGDDIRS=       $(DOMAINS:%=$(MSGROOT)/%)
MSGDIRS=        $(MSGROOT) $(MSGDDIRS) $(MSGROOT)/LC_TIME

all all_xmod :=		TARGET= all
install install_xmod :=	TARGET= install
install_h :=		TARGET= install_h
clean :=		TARGET= clean
clobber :=		TARGET= clobber
check :=		TARGET= check
_msg :=			TARGET= _msg


.KEEP_STATE:

#
# Note: install only builds the all target for the pkgdefs
#       directory.  We are not yet ready to have an install
#	build create 'packages' also.  To build packages
#	cd pkgdefs and do a 'make install'
#
all: sgs $(SUBDIRS) pkg_all
install: sgs $(SUBDIRS) pkg_all

clean clobber: $(SUBDIRS) head pkgdefs
_msg: _msgdirs rootdirs _msgheaders $(MSGSUBDIRS)

# for a complete build from scratch
crankturn: sgs uts pkg_all
	@cd lib; pwd; $(MAKE) install
	@cd stand; $(MAKE) all
	@cd psm; $(MAKE) all
	@cd uts/adb; pwd; $(MAKE) all
	@cd cmd; pwd; $(MAKE) all
	@cd ucblib; pwd; $(MAKE) install
	@cd ucbcmd; pwd; $(MAKE) all

pkg_all:
	@cd pkgdefs; pwd; $(MAKE) all

#
# target for building a proto area for reference via the ROOT macro
#
protolibs: rootlibs ucblibs

# build all ucb libraries
#
ucblibs:
	@cd ucblib; pwd; $(MAKE) install

# Base subset of rootproto, excluding ucb libraries
#
rootlibs: sgs
	@cd lib; pwd; $(MAKE) install

# create target-variant symlinks
links:
	@cd uts; pwd; $(MAKE) links
	@cd lib/libc; pwd; $(MAKE) links

$(SUBDIRS) head ucbhead pkgdefs: FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

.PARALLEL:	sysheaders userheaders libheaders cmdheaders ucbheaders

# librpcsvc has a dependency on headers installed by
# userheaders, hence the .WAIT before libheaders.
sgs: rootdirs .WAIT sysheaders userheaders .WAIT \
	libheaders cmdheaders ucbheaders

# /var/mail/:saved is a special case because of the colon in the name.
#
rootdirs: $(ROOTDIRS)
	$(INS) -d -m 775 $(ROOT)/var/mail/:saved
	$(CH)$(CHOWN) root $(ROOT)/var/mail/:saved
	$(CH)$(CHGRP) mail $(ROOT)/var/mail/:saved

_msgdirs:       $(MSGDIRS)

$(ROOTDIRS) $(MSGDIRS):
	$(INS.dir)

_msgheaders: userheaders sysheaders
	@cd lib/libintl; pwd; $(MAKE) install_h
	@cd lib/libtnf; pwd; $(MAKE) install_h
	@cd lib/libtnfprobe; pwd; $(MAKE) install_h

userheaders: FRC
	@cd head; pwd; $(MAKE) install_h

cmdheaders: FRC
	@cd cmd; pwd; $(MAKE) install_h

libheaders: FRC
	@cd lib; pwd; $(MAKE) install_h

sysheaders: FRC
	@cd uts; pwd; $(MAKE) install_h

ucbheaders: FRC
	@cd ucbhead; pwd; $(MAKE) install_h

# each xmod target depends on a corresponding MACH-specific pseudotarget
# before doing common xmod work
#
all_xmod install_xmod: $$@_$(MACH)
	@cd uts/common/sys; pwd; $(MAKE) svvs_h

all_xmod_sparc install_xmod_sparc: FRC
	@cd uts/sparc; pwd; \
	  $(MAKE) TARGET=$(TARGET) svvs pm wsdrv
	@cd uts/sun4m; pwd; $(MAKE) TARGET=$(TARGET) cpr pmc

all_xmod_i386 install_xmod_i386: FRC
	@cd uts/i86; pwd; $(MAKE) TARGET=$(TARGET) svvs

check:	$(CHKHDRSUBDIRS)

FRC:

