#
#ident	"@(#)Makefile	1.3	95/02/14 SMI"
#
# Copyright (c) 1990-93, by Sun Microsystems, Inc.
#
# uts/adb/platform/sparc/sun4u/Makefile
#

ADB_BASE_DIR =	../../..
ADB_PLAT_DIR =	../..
COMMONDIR =	$(ADB_BASE_DIR)/common
ISADIR =	$(ADB_BASE_DIR)/sparc

KARCH =		sun4u
PLATFORM =	$(KARCH)
ARCHOPTS =	-DMP -D__sparcv9

INCLUDES =	-I${SYSDIR}/${KARCH} -I${SYSDIR}/srmmu \
		-I${SYSDIR}/${MACH} -I${SYSDIR}/${MACH}/v9 -I${SYSDIR}/sun

SRCS =		ctx.adb hmetopmgrp.adb msgbuf.adb pmgrp.adb pmgseg.adb \
		mutex.adb regs.adb rwlock.adb sema.adb machpcb.adb

include $(ADB_PLAT_DIR)/Makefile.psm

.KEEP_STATE:

.PARALLEL:	$(SCRIPTS)

all: $(SCRIPTS)

install: all $(USR_PSM_ADB_DIR) .WAIT $(ROOTSCRIPTS)

include $(ADB_PLAT_DIR)/Makefile.targ
