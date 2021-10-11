#
#ident	"@(#)Makefile	1.5	94/03/18 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# uts/sun4u/vm/Makefile
#
UTSBASE	= ../..

#
# include global definitions
#
include ../Makefile.sun4u

#
# Override defaults.
#
FILEMODE	 = 644
OWNER = bin
GROUP = bin

HDRS=	mach_sfmmu.h hat_sfmmu.h

ROOTHDRS=		$(HDRS:%=$(USR_PSM_IVM_DIR)/%)

ROOTDIR=		$(ROOT)/usr/share/src
ROOTDIRS=		$(ROOTDIR)/uts $(ROOTDIR)/uts/$(PLATFORM)

ROOTLINK=		$(ROOTDIR)/uts/$(PLATFORM)/vm
LINKDEST=		../../../../platform/$(PLATFORM)/include/vm

CHECKHDRS=		$(HDRS:%.h=%.check)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS)

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS) $(ROOTLINK)

check:	$(CHECKHDRS)

#
# install rules
#
$(USR_PSM_IVM_DIR)/%: ../../sfmmu/vm/% $(USR_PSM_IVM_DIR)
	$(INS.file)

$(ROOTDIRS):
	$(INS.dir.bin.bin)

# -r because this used to be a directory and is now a link.
$(ROOTLINK):	$(ROOTDIRS)
	-$(RM) -r $@; $(SYMLINK) $(LINKDEST) $@ $(CHOWNLINK) $(CHGRPLINK)

%.check:	../../sfmmu/vm/%.h
	$(DOT_H_CHECK)

CLEANFILES=

CLOBBERFILES=

CLEANLINTFILES=

include ../../Makefile.targ
