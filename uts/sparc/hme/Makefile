#
# uts/sparc/hme/Makefile
#
# ident "@(#)Makefile 1.1	95/02/23 SMI"
#
# Copyright (c) 1993-1995, by Sun Microsystems, Inc.
#
#	This makefile drives the production of the hme driver
#	kernel module.
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= hme
OBJECTS		= $(HME_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(HME_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

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

all:		$(MODULE)

lint:		$(MODULE).lint

install:  	$(MODULE) $(ROOTMODULE)

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
