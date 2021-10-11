#
# uts/sparc/fas/Makefile
#
# ident	"@(#)Makefile 1.4	95/04/27 SMI"
#
# Copyright (c) 1993-1995, by Sun Microsystems, Inc.
#
#	This makefile drives the production of the fas driver kernel module.
#
#	sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE = ../..

#
#	Define the module and object file sets.
#
MODULE		= fas
OBJECTS		= $(FAS_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(FAS_OBJS:%.o=$(LINTS_DIR)/%.ln)
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
#	lock_lint rules
#
warlock: $(MODULE).ok reset_notify.ll

%.wlcmd:
	[ -f $@ ] || sccs get $@

fas.ok: fas.ll reset_notify.ll fas_callbacks.ll fas.wlcmd
	warlock -c fas.wlcmd fas.ll fas_callbacks.ll \
	reset_notify.ll  -l ddi_dki_impl -l scsi
	touch $@

%.ll: $(UTSBASE)/sun/io/scsi/adapters/%.c
	wlcc $(CFLAGS) $(CPPFLAGS) -o $@ $<

