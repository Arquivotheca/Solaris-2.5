#
# uts/sun4m/profile/Makefile
# Copyright (c) 1991, 1993 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.8	94/06/10 SMI"
#
#	This makefile only installs profile.conf (which is
#	platform dependent).
#
#	sun4m implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.  (Continue to need to define
#	ROOTMODULE to define the target for the .conf file, even though the
#	`ROOTMODULE' isn't created of installed by this Makefile.
#
MODULE		= profile
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun4m/io

#
#	Include common rules.
#
include $(UTSBASE)/sun4m/Makefile.sun4m

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(SRC_CONFILE)

install:	$(ROOT_PSM_DRV_DIR) $(ROOT_CONFFILE)

clean clobber lint:

#
#       No lints, so keep the clobber and clean targets reasonable.
#
CLEANLINTFILES	= Nothing_to_remove
CLEANFILES	= Nothing_to_remove
CLOBBERFILES	= Nothing_to_remove

#
#	Include common targets.
#
include $(UTSBASE)/sun4m/Makefile.targ
