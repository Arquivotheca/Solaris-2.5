#
# uts/sun4c/eeprom/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.8	93/07/14 SMI"
#
#	This makefile drives the production of the eeprom driver kernel module.
#
#	sun4c implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= eeprom
OBJECTS		= $(EEPROM_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(EEPROM_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4c/Makefile.sun4c

CLOBBERFILES	+= $(MODULE)

#
# lint pass one enforcement
#
CFLAGS += -v

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE)

lint:		$(MODULE).lint

install:  	$(MODULE) $(ROOTMODULE)

#
#	Include common targets.
#
include $(UTSBASE)/sun4c/Makefile.targ
