#
# uts/sparc/soc/Makefile
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile 1.3 94/10/31 SMI"
#
#	This makefile drives the production of the soc driver kernel
#	module, which is used for the Sparc Storage Array.
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
MODULE		= soc
OBJECTS		= $(SOC_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SOC_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

CLOBBERFILES	+= $(MODULE)

#
#	Default build targets.
#
.KEEP_STATE:

all browser:	$(MODULE)
browser:=	CFLAGS += -xsb

lint:		$(MODULE).lint

install:  	$(MODULE) $(ROOTMODULE)

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ

#
#	lock_lint rules
#
.INIT:
	@if [ ! -d wl ]; then mkdir wl; fi

wl/%: wl/%.ll
	chmod +x soc.wlcmd
	warlock -c soc.wlcmd $<
	touch $@

wl/%.ll: $(UTSBASE)/sun/io/scsi/adapters/soc.c
	wlcc $(CFLAGS) $(CPPFLAGS) -o $@ $<

warlock:	wl/$(MODULE)

