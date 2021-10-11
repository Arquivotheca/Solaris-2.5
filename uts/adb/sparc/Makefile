#
#ident	"@(#)Makefile	1.11	95/03/02 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# uts/adb/sparc/Makefile
#

ADB_BASE_DIR	= ..
ISADIR		= .
COMMONDIR	= $(ADB_BASE_DIR)/common
SYSDIR		= $(ADB_BASE_DIR)/..

SRCS +=		esp.adb		esp_cb_info.adb		esp_tagQ.adb \
		esp_cmd.adb \
		fas.adb		fas_cmd.adb		fas_tagQ.adb \
		fas_cb_info.adb	fas_regs.adb \
		dk_geom.adb	dk_label.adb		dk_vtoc.adb \
		scsi_disk.adb \
		isp.adb		isp_request.adb		isp_response.adb \
		isp_cmd.adb	scsi_tape.adb		st_drivetype.adb \
		rwindow.adb	ssa_disk.adb


include	$(COMMONDIR)/Makefile.com
