#
# uts/sparc/ramdisk/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.10	94/04/20 SMI"
#
#	This makefile drives the production of the ramdisk driver kernel
#	module.
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
MODULE		= ramdisk
OBJECTS		= $(RAMDISK_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(RAMDISK_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/io

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides.
#
CLOBBERFILES	+= $(MODULE)
CFLAGS		+= -v

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE) $(SRC_CONFILE)

lint:		$(MODULE).lint

#
#	Don't install. Simply a sample driver.
#
install:	all

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
