#
# uts/sun4u/genassym/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.3	95/02/14 SMI"
#
#	This makefile drives the production of assym.s through genassym.
#
#	sun4u implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Include common rules.
#
include $(UTSBASE)/sun4u/Makefile.sun4u

#
#	Default build targets.
#
.KEEP_STATE:

all:	$(DSF_DIR)/assym.s

#
#	genassym build.
#
$(DSF_DIR)/genassym:    $(GENASSYM_SRC)
	$(NATIVECC) $(ALWAYS_DEFS) $(GENASSYM_DEFS) $(NATIVE_INC_PATH) \
	-o $(DSF_DIR)/genassym $(GENASSYM_SRC)

#
#	Execute genassym to produce assym.s (should be assym.h).
#
$(DSF_DIR)/assym.s:     $(DSF_DIR)/genassym
	$(DSF_DIR)/genassym > $(DSF_DIR)/assym.s

#
#	Nothing to install.
#
install:	all

clean.lint:

clobber.lint:	clean.lint

clean:
	$(RM) $(DSF_DIR)/assym.s $(DSF_DIR)/genassym

clobber: clean
