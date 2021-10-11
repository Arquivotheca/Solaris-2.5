#
# uts/sun4u/sbus/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.5	95/02/14 SMI"
#
#	This makefile drives the production of the SBus driver kernel module,
#	the iommu nexus, and the obio nexus (which really is sbus on sun5).
#
#	sun4u implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= sbus
OBJECTS		= $(SYSIO_SBUS_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SYSIO_SBUS_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4u/Makefile.sun4u

CLOBBERFILES	+= $(MODULE)

# Turn this on once compiler understands v9 in it's backend
#INLINES		+= $(UTSBASE)/sun4u/io/iommu.il

#
# lint pass one enforcement
#
CFLAGS += -v

#
# Turn on doubleword alignment for 64 bit registers
#
CFLAGS += -dalign

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
include $(UTSBASE)/sun4u/Makefile.targ
