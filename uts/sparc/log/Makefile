#
# uts/sparc/log/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.8	94/06/10 SMI"
#
#	This makefile drives the production of the log driver 
#	kernel module.
#
#	sparc implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= log
OBJECTS		= $(LOG_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(LOG_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/io

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Override defaults to build a unique, local modstubs.o.
#
MODSTUBS_DIR	 = $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DLOG_MODULE
CLEANFILES	+= $(MODSTUBS_O)
CLOBBERFILES	+= $(MODULE)

#
# lint pass one enforcement
#
CFLAGS += -v

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
