#
# uts/sparc/krtld/Makefile
# Copyright (c) 1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.4	94/10/19 SMI"
#
#	kernel linker/loader makefile.
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
MODULE		= krtld
OBJECTS		= $(KRTLD_BOOT_OBJS:%=$(OBJS_DIR)/%) \
		  $(KRTLD_OBJS:%=$(OBJS_DIR)/%)
		  
LINTS		= $(KRTLD_BOOT_OBJS:%.o=$(LINTS_DIR)/%.ln) \
		  $(KRTLD_OBJS:%.o=$(LINTS_DIR)/%.ln)
PICS		= $(KRTLD_BOOT_OBJS)
ROOTMODULE	= $(ROOT_MISC_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides.
#
CLOBBERFILES	+= $(MODULE)
CFLAGS		+= -v
CPPFLAGS	+= -DELF_TARGET_SPARC -DBOOTSCRATCH
LINKFLAGS	= -e _kobj_boot
LD		+= $(LINKFLAGS)

$(PICS)		:= CFLAGS += -K pic

#
# Prevent GPROF flag and -xpg option from getting to krtld
#
$(MODULE)	:= EXTRA_CFLAGS=
$(MODULE)	:= EXTRA_OPTIONS=

#
#	MPSAS defines
#
$(MPSAS_BUILD)CFLAGS	+= -DMPSAS

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
