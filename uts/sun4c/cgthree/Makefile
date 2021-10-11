#
# uts/sun4c/cgthree/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.7	93/05/26 SMI"
#
#	This makefile drives the production of the cgthree driver kernel module.
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
MODULE		= cgthree
OBJECTS		= $(CGTHREE_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(CGTHREE_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4c/Makefile.sun4c

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
include $(UTSBASE)/sun4c/Makefile.targ
