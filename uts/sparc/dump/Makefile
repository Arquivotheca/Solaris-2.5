#
# uts/sparc/dump/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.7	94/04/20 SMI"
#
#	This makefile drives the production of the dump driver 
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
MODULE		= dump
OBJECTS		= $(DUMP_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(DUMP_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/os

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

all:		$(MODULE) $(SRC_CONFILE)

lint:		$(MODULE).lint

install:  	$(MODULE) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
