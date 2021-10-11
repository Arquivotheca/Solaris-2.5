#
# uts/sun4c/zsh/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.7	93/05/26 SMI"
#
#	This makefile drives the production of the sun4c "zsh" driver module.
#
#	sun4c implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= zsh
OBJECTS		= $(ZSH_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(ZSH_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun/io

#
#	Include common rules.
#
include $(UTSBASE)/sun4c/Makefile.sun4c

CLOBBERFILES	+= $(MODULE)
 
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
include $(UTSBASE)/sun4c/Makefile.targ
