#
#ident	"@(#)Makefile	1.21	94/12/06 SMI"
#
# Copyright (c) 1990-1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# uts/sun4c/sys/Makefile
#
UTSBASE	= ../..

#
# include global definitions
#
include ../Makefile.sun4c

#
# Override defaults.
#
FILEMODE	 = 644
OWNER = bin
GROUP = bin

HDRS= \
	auxio.h		buserr.h	clock.h				\
	ddi_subrdefs.h	eeprom.h	\
	enable.h	immu.h		intreg.h	machcpuvar.h	\
	machparam.h	machsystm.h	machthread.h	memerr.h	\
	mmu.h		prom_plat.h	pte.h		scb.h		\
	vm_machparam.h	zsmach.h

MONHDRS= eeprom.h	idprom.h	keyboard.h	password.h

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
	-$(INS.dir.bin.bin)

$(USR_PSM_MON_DIR)/%:	mon/% $(USR_PSM_MON_DIR)
	$(INS.file)

$(ROOTDIRS):
	$(INS.dir.bin.bin)

# -r because this used to be a directory and is now a link.
$(ROOTLINK):	$(ROOTDIRS)
	-$(RM) -r $@; $(SYMLINK) $(LINKDEST) $@ $(CHOWNLINK) $(CHGRPLINK)

mon/%.check:		mon/%.h
	$(DOT_H_CHECK)

include ../../Makefile.targ
