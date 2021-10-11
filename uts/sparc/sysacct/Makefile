#
# uts/sparc/sysacct/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.6	94/04/20 SMI"
#
#	This makefile drives the production of the sysacct driver kernel module.
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
MODULE		= sysacct
OBJECTS		= $(SYSACCT_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SYSACCT_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_SYS_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Override defaults to build a unique, local modstubs.o.
#
MODSTUBS_DIR	 = $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DSYSACCT_MODULE
CLEANFILES	+= $(MODSTUBS_O)

CLOBBERFILES	+= $(MODULE)
CFLAGS		+= -v

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
