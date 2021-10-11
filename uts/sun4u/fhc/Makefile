#
# uts/sun4u/fhc/Makefile
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile 1.4	95/02/16 SMI"
#
#	This makefile drives the production of the fhc driver kernel module.
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
MODULE		= fhc
OBJECTS		= $(FHC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(FHC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4u/Makefile.sun4u

CLOBBERFILES	+= $(MODULE)

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
