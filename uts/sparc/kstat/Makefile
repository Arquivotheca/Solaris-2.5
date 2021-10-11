#
# uts/sparc/kstat/Makefile
# Copyright (c) 1992-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.4	94/04/20 SMI"
#
#	kernel statistics driver makefile.
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
MODULE		= kstat
OBJECTS		= $(KSTAT_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(KSTAT_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/common/io

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
