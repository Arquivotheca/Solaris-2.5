#
# uts/sparc/sd/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.19	94/06/02 SMI"
#
#	This makefile drives the production of the sd driver kernel module.
#
#	sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= sd
OBJECTS		= $(SD_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SD_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun/io/scsi/targets

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides.
#
CLOBBERFILES	+= $(MODULE)
 
#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE) $(SRC_CONFILE)

lint:		$(MODULE).lint

install:	$(MODULE) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Include common targets.
#
include	$(UTSBASE)/sparc/Makefile.targ


#
#	Warlock
#

WARLOCK_COMMON_FILES = $(MODULE).ll sdwatch.ll $(SCSI_OBJS:%.o=../scsi/%.ll)
WARLOCK_ESP_FILES = $(WARLOCK_COMMON_FILES) ../esp/esp.ll
WARLOCK_ISP_FILES = $(WARLOCK_COMMON_FILES) ../isp/isp.ll

SD_FILES = $(MODULE).ll sdwatch.ll


warlock: warlock_alone warlock_with_esp warlock_with_isp

warlock_alone: $(MODULE).ok

warlock_with_esp: scsi_files esp_files $(SD_FILES)
	sccs get sd_with_hba.wlcmd
	chmod +x ./sd_with_hba.wlcmd 
	warlock -c ./sd_with_hba.wlcmd $(WARLOCK_ESP_FILES)

warlock_with_isp:  scsi_files isp_files $(SD_FILES)
	sccs get sd_with_hba.wlcmd
	chmod +x ./sd_with_hba.wlcmd 
	warlock -c ./sd_with_hba.wlcmd $(WARLOCK_ISP_FILES)

scsi_files:
	cd ../scsi; make warlock;

esp_files:
	cd ../esp; make warlock;

isp_files:
	cd ../isp; make warlock;

%.ok: %.ll
	sccs get sd.wlcmd
	chmod +x sd.wlcmd
	warlock -c ./sd.wlcmd $<
	touch $@
 
%.ll: $(UTSBASE)/sun/io/scsi/targets/%.c
	wlcc $(CFLAGS) $(CPPFLAGS) -o $@ $<
