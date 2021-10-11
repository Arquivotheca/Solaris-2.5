#
#ident	"@(#)Makefile	1.3	93/11/19 SMI"
#
# Copyright (c) 1990-93, by Sun Microsystems, Inc.
#
# uts/adb/platform/sparc/sun4/Makefile
#

ADB_PLAT_DIR =	../..

KARCH =		sun4
PLATFORM =	$(KARCH)

INCLUDES =	-I${SYSDIR}/${KARCH} -I${SYSDIR}/sunmmu \
		-I${SYSDIR}/${MACH} -I${SYSDIR}/sun

SRCS =		ctx.adb hmetopmgrp.adb msgbuf.adb pmgrp.adb pmgseg.adb

include $(ADB_PLAT_DIR)/Makefile.psm

.KEEP_STATE:

.PARALLEL:	$(SCRIPTS)

all: $(SCRIPTS)

install: all $(USR_PSM_ADB_DIR) .WAIT $(ROOTSCRIPTS)

include $(ADB_PLAT_DIR)/Makefile.targ
