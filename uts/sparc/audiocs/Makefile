#
# uts/sparc/audiocs/Makefile
# Copyright (c) 1993-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.2	94/04/20 SMI"
#
#	This makefile drives the production of the Crystal 4231
#	audio driver (audiocs) kernel module.
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
MODULE		= audiocs
OBJECTS		= $(AUDIOCS_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(AUDIOCS_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides
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

$(MODULE):

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
