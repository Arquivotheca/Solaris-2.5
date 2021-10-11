#
# uts/sparc/consconfig/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.7	94/10/12 SMI"
#
#	This makefile drives the production of the consconfig `misc'
#	kernel module.
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
MODULE		= consconfig
OBJECTS		= $(CONSCONFIG_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(CONSCONFIG_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_MISC_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides.
#
MODSTUBS_DIR	 = $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DCONSCONFIG_MODULE
CLEANFILES	+= $(MODSTUBS_O)

CLOBBERFILES	+= $(MODULE)

#
#	MPSAS defines
#
$(MPSAS_BUILD)CFLAGS	+= -DMPSAS

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
