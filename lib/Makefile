#
#pragma ident	"@(#)Makefile	1.89	95/02/22 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/Makefile
#
# include global definitions
include ../Makefile.master

# Note that:
#	libdl must preceed the other libraries (or at least libc and libnsl)
#	libw must preceed libintl (for ws*() functions)
#	libintl must preceed libc (for messaging)
#	libc must preceed libcurses (for utilities)
#	libadm must preceed libelf (for nlist)
#	libresolv must preceed libsocket (dependencies)
#	libnsl must preceed librac
#
#	Note that libcurses installs commands along with its library.
#	This is a minor bug which probably should be fixed.
#
#	Also note that a few extra libraries are kept in cmd source.

#
# NOTE: libvolmgt depends on libgen so libgen must be compiled first
#	(see libvolmgt/Makefile for how libgen is used)
#

COMMON_SUBDIRS= \
	libdl		\
	libdl_stubs	\
	libelf		\
	libw		\
	libintl		\
	libc		\
	libmp		\
	libTL		\
	libaio		\
	libadm		\
	libbsdmalloc	\
	libcmd		\
	libcrypt	\
	libcurses	\
	libdoor		\
	libeti		\
	libgen		\
	libgenIO	\
	libkstat	\
	libkvm		\
	liblm		\
	libmalloc	\
	libmapmalloc	\
	libnls		\
	libnsl		\
	libpkg		\
	libplot		\
	libsocket	\
	libresolv	\
	librpcsvc	\
	libsys		\
	libnisdb	\
	libkrb		\
	nametoaddr	\
	nsswitch	\
	librac		\
	libthread	\
	libauth		\
	scheme		\
	libbsm		\
	libposix4	\
	libvolmgt	\
	libdevinfo	\
	libc2		\
	libthread_db	\
	libsec		\
	libtnfprobe	\
	libtnf		\
	fn		\
	libpthread

sparc_SUBDIRS= \
	libc_psr

SUBDIRS= $(COMMON_SUBDIRS) $($(MACH)_SUBDIRS)

#
# For message catalogue file creation
#
MSGSUBDIRS= libc libcmd libnsl libpkg libcurses

BWOSDIRS= libg libm libmsg libwindows

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=		TARGET= _msg

.KEEP_STATE:

all clean clobber: $(SUBDIRS)

install: $(SUBDIRS) install_extra

# extra libraries kept in other source areas
install_extra:
	@cd ../cmd/mail; pwd; $(MAKE) install_lib
	@cd ../cmd/sgs; pwd; $(MAKE) install_lib
	@pwd

install_h check:
	@cd libc; pwd; $(MAKE) $@
	@cd libcurses; pwd; $(MAKE) $@
	@cd libeti; pwd; $(MAKE) $@
	@cd libgen; pwd; $(MAKE) $@
	@cd libintl; pwd; $(MAKE) $@
	@cd libkstat; pwd; $(MAKE) $@
	@cd libkvm; pwd; $(MAKE) $@
	@cd libw; pwd; $(MAKE) $@
	@cd librpcsvc; pwd; $(MAKE) $@
	@cd libkrb; pwd; $(MAKE) $@
	@cd libaio; pwd; $(MAKE) $@
	@cd libbsm; pwd; $(MAKE) $@
	@cd libvolmgt; pwd; $(MAKE) $@
	@cd libdevinfo; pwd; $(MAKE) $@
	@cd libtnfprobe; pwd; $(MAKE) $@
	@cd libtnf; pwd; $(MAKE) $@
	@cd fn; pwd; $(MAKE) $@

_msg: $(MSGSUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
