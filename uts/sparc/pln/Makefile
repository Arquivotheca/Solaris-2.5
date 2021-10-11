#
# uts/sparc/pln/Makefile
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile 1.2 94/10/31 SMI"
#
#	This makefile drives the production of the pln driver kernel module.
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
MODULE		= pln
OBJECTS		= $(PLN_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(PLN_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun/io/scsi/adapters


#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

CLOBBERFILES	+= $(MODULE)

#
#	Default build targets.
#
.KEEP_STATE:

all browser:		$(MODULE) $(SRC_CONFILE)

browser := CFLAGS += -xsb

lint:		$(MODULE).lint

install:  	$(MODULE) $(ROOTMODULE) $(ROOT_CONFFILE)


#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
