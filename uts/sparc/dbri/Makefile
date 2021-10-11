#
# uts/sparc/dbri/Makefile
# Copyright (c) 1992-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.2	94/04/20 SMI"
#
#	This makefile drives the production of the sparc "dbri" driver module.
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
MODULE		= dbri
OBJECTS		= $(DBRI_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(DBRI_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides
#
CLOBBERFILES	+= $(MODULE)

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE)

lint:		$(MODULE).lint

install:	$(MODULE) $(ROOTMODULE)

#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
