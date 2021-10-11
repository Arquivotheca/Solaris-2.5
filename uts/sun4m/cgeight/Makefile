#
# uts/sun4m/cgeight/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.3	93/05/26 SMI"
#
#	This makefile drives the production of cgeight (tc) display driver
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
MODULE		= cgeight
OBJECTS		= $(CGEIGHT_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(CGEIGHT_OBJS:%.o=$(LINTS_DIR)/%.ln)
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
