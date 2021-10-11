#
# uts/sparc/sx_cmem/Makefile
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#ident  "@(#)Makefile 1.4    93/05/26 SMI" 
#
#	This makefile drives the production of the SX contiguous memory driver
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= sx_cmem
OBJECTS		= $(SX_CMEM_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SX_CMEM_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun4m/io

#
#	Include sun4m rules.
#
include $(UTSBASE)/sun4m/Makefile.sun4m

CLEANFILES	= $(OBJS_DIR)/$(MODULE).o

CLOBBERFILES	+= $(MODULE)

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE)

lint:		$(MODULE).lint

install:	$(MODULE) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Include common targets.
#
include $(UTSBASE)/sun4m/Makefile.targ
