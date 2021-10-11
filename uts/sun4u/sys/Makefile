#
#ident	"@(#)Makefile	1.21	95/06/01 SMI"
#
# Copyright (c) 1991-1993, by Sun Microsystems, Inc.
#
# uts/sun4u/sys/Makefile
#
UTSBASE	= ../..

#
# include global definitions
#
include ../Makefile.sun4u

#
# Override defaults.
#
FILEMODE	 = 644
OWNER = bin
GROUP = bin

HDRS=	\
	ac.h		bt.h		clock.h		dvma.h		\
	eeprom.h	environ.h	fhc.h		intr.h		\
	intreg.h	ivintr.h	iocache.h	iommu.h		\
	machcpuvar.h	machparam.h	machsystm.h	machthread.h	\
	module.h 	nexusdebug.h	pci_regs.h	pci_iommu.h	\
	pci_var.h 	prom_debug.h	prom_plat.h	scb.h		\
	simmstat.h 	spitasi.h	spitregs.h	sram.h		\
	sysctrl.h	sysioerr.h	sysiosbus.h	vm_machparam.h	\
	xc_impl.h	x_call.h	zsmach.h

XHDRS=

MONHDRS=	
#MONHDRS=	eeprom.h    idprom.h    keyboard.h  password.h

SFMMUHDRS=	mmu.h	pte.h

USR_PSM_MON_DIR=	$(USR_PSM_ISYS_DIR)/mon

ROOTHDRS=		$(HDRS:%=$(USR_PSM_ISYS_DIR)/%)
ROOTMONHDRS=		$(MONHDRS:%=$(USR_PSM_MON_DIR)/%)
ROOTXHDRS=		$(XHDRS:%=$(ROOTDIR)/uts/sun4m/sys/%)
ROOTSRMMUHDRS=		$(SRMMUHDRS:%=$(USR_PSM_ISYS_DIR)/%)

ROOTDIR=		$(ROOT)/usr/share/src
ROOTDIRS=		$(ROOTDIR)/uts $(ROOTDIR)/uts/$(PLATFORM)

ROOTLINK=		$(ROOTDIR)/uts/$(PLATFORM)/sys
LINKDEST=		../../../../platform/$(PLATFORM)/include/sys

CHECKHDRS=		$(HDRS:%.h=%.check) \
			$(SFMMUHDRS:%.h=%.check) \
			$(MONHDRS:%.h=mon/%.check) \
			$(XHDRS:%.h=%.check)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS) $(ROOTSFMMUHDRS) $(ROOTMONHDRS)

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS) $(ROOTSFMMUHDRS) $(ROOTMONHDRS) $(ROOTLINK)

check:	$(CHECKHDRS)

#
# install rules
#
$(USR_PSM_MON_DIR):	$(USR_PSM_ISYS_DIR)
	$(INS.dir.bin.bin)

$(USR_PSM_ISYS_DIR)/%:	../../srmmu/sys/% $(USR_PSM_ISYS_DIR)
	$(INS.file)

$(USR_PSM_MON_DIR)/%:	mon/% $(USR_PSM_MON_DIR)
	$(INS.file)

$(ROOTDIRS):
	$(INS.dir.bin.bin)

# -r because this used to be a directory and is now a link.
$(ROOTLINK):	$(ROOTDIRS)
	-$(RM) -r $@; $(SYMLINK) $(LINKDEST) $@ $(CHOWNLINK) $(CHGRPLINK)

mon/%.check:	mon/%.h
	$(DOT_H_CHECK)

%.check:	../../sfmmu/sys/%.h
	$(DOT_H_CHECK)

FRC:

include ../../Makefile.targ
