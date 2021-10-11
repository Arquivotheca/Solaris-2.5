#
# uts/sparc/scsi/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.6	94/04/20 SMI"
#
#	This makefile drives the production of the scsi kernel module.
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
MODULE		= scsi
OBJECTS		= $(SCSI_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(SCSI_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_MISC_DIR)/$(MODULE)

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
%.ll: $(UTSBASE)/common/io/scsi/impl/%.c
	wlcc $(CFLAGS) $(CPPFLAGS) -o $@ $<

%.ll: $(UTSBASE)/common/io/scsi/conf/%.c
	wlcc $(CFLAGS) $(CPPFLAGS) -o $@ $<

%.ok: %.ll
	warlock $<
	touch $@

WARLOCK_OBJECTS	= $(SCSI_OBJS:%.o=%.ll)

warlock: $(WARLOCK_OBJECTS)
