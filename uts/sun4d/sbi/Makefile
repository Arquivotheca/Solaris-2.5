#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#

#ident	"@(#)Makefile	1.12	93/05/26 SMI"

#
#	This makefile drives the production of the SBus driver kernel module.
#
#	sun4d implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= sbi
OBJECTS		= $(SBI_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SBI_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4d/Makefile.sun4d

CLOBBERFILES	+= $(MODULE)

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
include $(UTSBASE)/sun4d/Makefile.targ
