#
# uts/sparc/telmod/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.3	94/12/09 SMI"
#
#	This makefile drives the production of the telmod streams kernel
#	module.
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
MODULE		= telmod
OBJECTS		= $(TELMOD_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(TELMOD_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_STRMOD_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

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
