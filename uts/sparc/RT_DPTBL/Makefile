#
# uts/sparc/RT_DPTBL/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.5	94/04/20 SMI"
#
#	This makefile drives the production of the RT_DPTBL driver kernel module.
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
MODULE		= RT_DPTBL
OBJECTS		= $(RT_DPTBL_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(RT_DPTBL_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_SCHED_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides.
#
MODSTUBS_DIR	= $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DRT_DPTBL_MODULE
CLEANFILES	+= $(MODSTUBS_O)

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
