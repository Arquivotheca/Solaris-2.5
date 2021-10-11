#
# uts/sun4c/obpsym/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.5	94/11/16 SMI"
#
#	This makefile drives the production of the obpsym misc kernel
#	module.
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= obpsym
OBJECTS		= $(OBPSYM_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(OBPSYM_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_MISC_DIR)/$(MODULE)

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
