#
# uts/sparc/genunix/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.3	94/09/29 SMI"
#
#	This makefile drives the production of the generic
#	unix kernel module.
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
MODULE		= genunix

OBJECTS		= $(GENUNIX_OBJS:%=$(OBJS_DIR)/%) \
		  $(NOT_YET_KMODS:%=$(OBJS_DIR)/%)

LINTS		= $(GENUNIX_OBJS:%.o=$(LINTS_DIR)/%.ln) \
		  $(NOT_YET_KMODS:%.o=$(LINTS_DIR)/%.ln)

PLATFORM	= sun4u
ROOTMODULE	= $(ROOT_PSM_MOD_DIR)/$(MODULE)

LIBGEN		= libgenunix.so
LIBSTUBS	= $(GENSTUBS_OBJS:%=$(OBJS_DIR)/%)
SONAME		= $(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Include v9 files
#
include $(UTSBASE)/sparc/v9/Makefile.files

CLEANFILES	+= $(LIBSTUBS)
CLOBBERFILES	+= $(MODULE) $(LIBGEN)

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(LIBGEN)

$(LIBGEN):	$(MODULE) $(LIBSTUBS)
		$(LD) -o $@ -G -h $(SONAME) $(MODULE) $(LIBSTUBS)

lint:		$(MODULE).lint

install:  	$(MODULE) $(ROOTMODULE)

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ

#
#	Include v9 rules.
#
include $(UTSBASE)/sparc/v9/Makefile.rules
