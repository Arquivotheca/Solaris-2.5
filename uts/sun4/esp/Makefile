#
# uts/sun4/esp/Makefile
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#pragma ident	"@(#)Makefile	1.1	93/06/14 SMI"
#
#	This makefile only installs a special esp.conf as required on sun4
#	platforms (specifically 4/330).
#
#	sun4 implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.  (Continue to need to define
#	ROOTMODULE to define the target for the .conf file, even though the
#	`ROOTMODULE' isn't created of installed by this Makefile.)
#
MODULE		= esp
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)
CONF_SRCDIR	= $(UTSBASE)/sun4/io/scsi/adapters

#
#	Include common rules.
#
include $(UTSBASE)/sun4/Makefile.sun4

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
include $(UTSBASE)/sun4/Makefile.targ
