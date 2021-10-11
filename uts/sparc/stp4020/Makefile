#
# uts/sparc/stp4020/Makefile
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
#ident "@(#)Makefile	1.5	95/02/01 SMI"
#
#	This makefile drives the production of the DoRight driver kernel module.
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
MODULE		= stp4020
OBJECTS		= $(STP4020_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(STP4020_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

CLOBBERFILES    += $(MODULE)

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

install:	$(MODULE) $(ROOTMODULE)

#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
