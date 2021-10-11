#
# uts/sun4/ncr/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.6	93/05/26 SMI"
#
#	This makefile drives the production of the ncr driver kernel module.
#
#	sun4 implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= ncr
OBJECTS		= $(NCR_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(NCR_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun/io/scsi/adapters


#
#	Include common rules.
#
include $(UTSBASE)/sun4/Makefile.sun4

#
#	Override defaults to build a unique, local modstubs.o.
#
MODSTUBS_DIR	 = $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DNCR_MODULE
CLEANFILES	+= $(MODSTUBS_O)
CLOBBERFILES	+= $(MODULE)

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE) $(SRC_CONFILE)

lint:		$(MODULE).lint

install:	$(MODULE) $(ROOTMODULE) $(ROOT_CONFFILE)

#
#	Include common targets.
#
include $(UTSBASE)/sun4/Makefile.targ
