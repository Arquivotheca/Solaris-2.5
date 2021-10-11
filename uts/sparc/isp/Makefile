#
# uts/sparc/isp/Makefile
# Copyright (c) 1993-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.8	94/10/20 SMI"
#
#	This makefile drives the production of the isp driver kernel module.
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
MODULE		= isp
OBJECTS		= $(ISP_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(ISP_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides.
#
CLOBBERFILES	+= $(MODULE)

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

#
#	Warlock targets
#

warlock:	$(MODULE).ok

%.ok: %.ll
	sccs get isp.wlcmd
	chmod +x isp.wlcmd
	warlock -c ./isp.wlcmd $<
	touch $@

%.ll: $(UTSBASE)/sun/io/scsi/adapters/isp.c
	wlcc $(CFLAGS) $(CPPFLAGS) -o $@ $<
