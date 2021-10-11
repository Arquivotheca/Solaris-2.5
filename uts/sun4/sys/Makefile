#
#ident	"@(#)Makefile	1.22	93/07/14 SMI"
#
# Copyright (c) 1989-1993, by Sun Microsystems, Inc.
#
# uts/sun4/sys/Makefile
#
UTSBASE	= ../..

#
# include global definitions
#
include ../Makefile.sun4

#
# Override defaults.
#
FILEMODE	 = 644
OWNER = bin
GROUP = bin

HDRS= \
	buserr.h	clock.h		diag.h		eccreg.h	\
	eeprom.h	enable.h	immu.h		intreg.h	\
	iocache.h	machcpuvar.h	machparam.h	machsystm.h	\
	machthread.h	memerr.h	mmu.h		pte.h		\
	scb.h		vm_machparam.h	zsmach.h

MONHDRS=	idprom.h	keyboard.h	password.h 

USR_PSM_MON_DIR=	$(USR_PSM_ISYS_DIR)/mon

ROOTHDRS=		$(HDRS:%=$(USR_PSM_ISYS_DIR)/%)
ROOTMONHDRS=		$(MONHDRS:%=$(USR_PSM_MON_DIR)/%)

ROOTDIR=		$(ROOT)/usr/share/src
ROOTDIRS=		$(ROOTDIR)/uts $(ROOTDIR)/uts/$(PLATFORM)

ROOTLINK=		$(ROOTDIR)/uts/$(PLATFORM)/sys
LINKDEST=		../../../../platform/$(PLATFORM)/include/sys

CHECKHDRS=		$(HDRS:%.h=%.check) \
			$(MONHDRS:%.h=mon/%.check)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS) $(ROOTMONHDRS)

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS) $(ROOTMONHDRS) $(ROOTLINK)

check:	$(CHECKHDRS)

#
# install rules
#
$(USR_PSM_MON_DIR):	$(USR_PSM_ISYS_DIR)
	$(INS.dir.bin.bin)

$(USR_PSM_MON_DIR)/%:	mon/% $(USR_PSM_MON_DIR)
	$(INS.file)

$(ROOTDIRS):
	$(INS.dir.bin.bin)

# -r because this used to be a directory and is now a link.
$(ROOTLINK):	$(ROOTDIRS)
	-$(RM) -r $@; $(SYMLINK) $(LINKDEST) $@ $(CHOWNLINK) $(CHGRPLINK)

mon/%.check:	mon/%.h
	$(DOT_H_CHECK)

include ../../Makefile.targ
