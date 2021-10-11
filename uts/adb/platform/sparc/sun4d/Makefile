#
#ident	"@(#)Makefile	1.6	95/01/16 SMI"
#
# Copyright (c) 1990-93, by Sun Microsystems, Inc.
#
# uts/adb/platform/sparc/sun4d/Makefile
#

ADB_BASE_DIR =	../../..
ADB_PLAT_DIR =	../..
COMMONDIR =	$(ADB_BASE_DIR)/common
ISADIR =	$(ADB_BASE_DIR)/sparc

KARCH =		sun4d
PLATFORM =	$(KARCH)
ARCHOPTS =	-DMP

INCLUDES =	-I${SYSDIR}/${KARCH} -I${SYSDIR}/srmmu \
		-I${SYSDIR}/${MACH} -I${SYSDIR}/${MACH}/v7 -I${SYSDIR}/sun

SRCS =		ctx.adb hmetopmgrp.adb msgbuf.adb pmgrp.adb pmgseg.adb \
		mutex.adb regs.adb rwlock.adb sema.adb machpcb.adb \
		srmmu.adb ptbl.adb srhme.adb

include $(ADB_PLAT_DIR)/Makefile.psm

.KEEP_STATE:

.PARALLEL:	$(SCRIPTS)

all: $(SCRIPTS)

install: all $(USR_PSM_ADB_DIR) .WAIT $(ROOTSCRIPTS)

include $(ADB_PLAT_DIR)/Makefile.targ
