#
# uts/sparc/devfs/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.11	93/10/22 SMI"
#
#	This makefile drives the production of the devfs support files
#
#	sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Define the module and object file sets.
#
SRCDIR		= $(UTSBASE)/sparc/os
DRVFILES	= name_to_major minor_perm driver_aliases name_to_sysnum driver_classes
PTIFILE		= path_to_inst
FILEMODE	= 644
OWNER		= root
GROUP		= sys
ROOTETC		= $(ROOT)/etc

SRCFILES	= $(DRVFILES:%=$(SRCDIR)/%) $(PTIFILE:%=$(SRCDIR)/%)
ROOTDRVFILES	= $(DRVFILES:%=$(ROOTETC)/%)
ROOTPTIFILES	= $(PTIFILE:%=$(ROOTETC)/%)

#
#	Default build targets.
#
.KEEP_STATE:

all: $(SRCFILES)

install: all $(ROOTDRVFILES) $(ROOTPTIFILES)

#
#	The ROOTETC directory is made by ../../../Targetdirs
#	through the rootdirs target in ../../../Makefile.
#
$(ROOTETC)/%: $(SRCDIR)/%
	$(INS.file)

clean clobber lint:

# 
#	No lints, so keep the clobber and clean targets reasonable. 
# 
CLEANLINTFILES	= Nothing_to_remove
CLEANFILES	= Nothing_to_remove
CLOBBERFILES	= Nothing_to_remove

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ
