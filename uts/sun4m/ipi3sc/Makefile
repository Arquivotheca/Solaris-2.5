#
# uts/sun4m/ipi3sc/Makefile
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.4	93/05/26 SMI"
#
#	This makefile drives the production of the ipi3sc driver
#	kernel module.
#
#	sun4m implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= ipi3sc
OBJECTS		= $(IPI3SC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(IPI3SC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4m/Makefile.sun4m

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
include $(UTSBASE)/sun4m/Makefile.targ
