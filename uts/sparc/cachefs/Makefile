#
# uts/sparc/cachefs/Makefile
# Copyright (c) 1992-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.9	94/06/07 SMI"
#
#	This makefile drives the production of the Cache file system
#	kernel module.
#
#	sparc implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= cachefs
OBJECTS		= $(CACHEFS_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(CACHEFS_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_FS_DIR)/$(MODULE)

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
