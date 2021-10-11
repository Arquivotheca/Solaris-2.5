#
# Copyright (c) 1990-1994, by Sun Microsystems, Inc.
# All rights reserved.
#
#ident	"@(#)Makefile	1.26	94/12/06 SMI"
#
# uts/sun4d/sys/Makefile
#
UTSBASE = ../..

#
# include global definitions
#
include ../Makefile.sun4d

#
# Override defaults.
#
FILEMODE	 = 644
OWNER = bin
GROUP = bin

HDRS= \
	bcopy_if.h	bt.h		clock.h		ddi_subrdefs.h	\
	devaddr.h	eeprom.h	hwbkpt.h			\
	immu.h		iocache.h	iommu.h		led.h		\
	machcpuvar.h	machparam.h	machsystm.h	machthread.h	\
	memerr.h	mqh.h		nvsimm.h			\
	physaddr.h	prom_plat.h	scb.h		syserr.h	\
	vm_machparam.h	xdb_inline.h	zsmach.h

SRMMU_HDRS= \
	mmu.h		pte.h

ROOTHDRS=		$(HDRS:%=$(USR_PSM_ISYS_DIR)/%)
ROOTSRMMUHDRS=		$(SRMMU_HDRS:%=$(USR_PSM_ISYS_DIR)/%)

ROOTDIR=		$(ROOT)/usr/share/src
ROOTDIRS=		$(ROOTDIR)/uts $(ROOTDIR)/uts/$(PLATFORM)

ROOTLINK=		$(ROOTDIR)/uts/$(PLATFORM)/sys
LINKDEST=		../../../../platform/$(PLATFORM)/include/sys

CHECKHDRS=		$(HDRS:%.h=%.check) \
			$(SRMMU_HDRS:%.h=%.check)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS) $(ROOTSRMMUHDRS)

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS) $(ROOTSRMMUHDRS) $(ROOTLINK)

check:	$(CHECKHDRS)

#
# install rules
#
$(USR_PSM_ISYS_DIR)/%:	../../srmmu/sys/% $(USR_PSM_ISYS_DIR)
	$(INS.file)

$(ROOTDIRS):
	$(INS.dir.bin.bin)

# -r because this used to be a directory and is now a link.
$(ROOTLINK):	$(ROOTDIRS)
	-$(RM) -r $@; $(SYMLINK) $(LINKDEST) $@ $(CHOWNLINK) $(CHGRPLINK)

%.check:	../../srmmu/sys/%.h
	$(DOT_H_CHECK)

include ../../Makefile.targ
