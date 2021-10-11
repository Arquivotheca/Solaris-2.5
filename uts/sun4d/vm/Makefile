#
#ident	"@(#)Makefile	1.10	93/08/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# uts/sun4d/vm/Makefile
#
UTSBASE	= ../..

#
# include global definitions
#
include ../Makefile.sun4d

#
# Override defaults.
#
FILEMODE	 = 644
OWNER = bin
GROUP = bin

HDRS=	mach_srmmu.h hat_srmmu.h

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
$(USR_PSM_IVM_DIR)/%: ../../srmmu/vm/% $(USR_PSM_IVM_DIR)
	$(INS.file)

$(ROOTDIRS):
	$(INS.dir.bin.bin)

# -r because this used to be a directory and is now a link.
$(ROOTLINK):	$(ROOTDIRS)
	-$(RM) -r $@; $(SYMLINK) $(LINKDEST) $@ $(CHOWNLINK) $(CHGRPLINK)

%.check:	../../srmmu/vm/%.h
	$(DOT_H_CHECK)

CLEANFILES=

CLOBBERFILES=

CLEANLINTFILES=

include ../../Makefile.targ
