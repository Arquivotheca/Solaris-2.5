#
# uts/sun4m/kb/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.3	93/05/26 SMI"
#
#	This makefile drives the production of the kb streams kernel
#	module.
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
MODULE		= kb
OBJECTS		= $(KB_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(KB_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_STRMOD_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4m/Makefile.sun4m

#
#	Override defaults to build a unique, local modstubs.o.
#
MODSTUBS_DIR	 = $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DKB_MODULE
CLEANFILES	+= $(MODSTUBS_O)
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
