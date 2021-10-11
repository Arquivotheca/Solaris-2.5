#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#

#ident	"@(#)Makefile	1.7	94/06/02 SMI"

#
#	This makefile drives the production of the rootnex driver
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
MODULE		= rootnex
OBJECTS		= $(ROOTNEX_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(ROOTNEX_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4d/Makefile.sun4d
MODSTUBS_DIR	 = $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DROOTNEX_MODULE
CLEANFILES	+= $(MODSTUBS_O)
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


install:	$(MODULE) $(ROOTMODULE)

#
#	Include common targets.
#
include $(UTSBASE)/sun4d/Makefile.targ
