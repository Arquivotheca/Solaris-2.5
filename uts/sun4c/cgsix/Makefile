#
# uts/sun4c/cgsix/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.8	93/05/26 SMI"
#
#	This makefile drives the production of cgsix (lego) display driver
#	kernel module.
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
MODULE		= cgsix
OBJECTS		= $(CGSIX_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(CGSIX_OBJS:%.o=$(LINTS_DIR)/%.ln)
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
