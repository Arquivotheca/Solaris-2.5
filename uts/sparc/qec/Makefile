#
# uts/sparc/qec/Makefile
# Copyright (c) 1993-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.3	94/04/20 SMI"
#
#	This makefile drives the production of the qec driver
#	kernel module.
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= qec
OBJECTS		= $(QEC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(QEC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides.
#
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
include $(UTSBASE)/sparc/Makefile.targ
