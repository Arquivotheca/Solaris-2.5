#
# uts/sun4u/stc/Makefile
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#ident  "@(#)Makefile 1.3     95/02/14 SMI"
#
#	This makefile drives the production of stc (spiff, sdc, spif,
#	spc/s) serial / parallel SBus adapter card kernel module
#
#	sun4u implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= stc
OBJECTS		= $(STC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(STC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun/io

#
#	Include common rules.
#
include $(UTSBASE)/sun4u/Makefile.sun4u

CLOBBERFILES	+= $(MODULE)

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE)

lint:		$(MODULE).lint


install:  	$(MODULE) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Include common targets.
#
include $(UTSBASE)/sun4u/Makefile.targ
