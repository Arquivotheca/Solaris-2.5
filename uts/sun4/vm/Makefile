#
#ident	"@(#)Makefile	1.1	93/08/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# uts/sun4/vm/Makefile
#
UTSBASE	= ../..

#
# include global definitions
#
include ../Makefile.sun4

#
# Override defaults.
#
FILEMODE	 = 644
OWNER = bin
GROUP = bin

HDRS=	hat_sunm.h

ROOTHDRS=		$(HDRS:%=$(USR_PSM_IVM_DIR)/%)

ROOTDIR=		$(ROOT)/usr/share/src
ROOTDIRS=		$(ROOTDIR)/uts $(ROOTDIR)/uts/$(PLATFORM)

ROOTLINK=		$(ROOTDIR)/uts/$(PLATFORM)/vm
LINKDEST=		../../../../platform/$(PLATFORM)/include/vm

CHECKHDRS=		$(HDRS:%.h=%.check)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(USR_PSM_IVM_DIR) $(ROOTHDRS) $(ROOTDIRS) $(ROOTLINK)

check:	$(CHECKHDRS)

#
# install rules
#
$(USR_PSM_IVM_DIR)/%: ../../sunmmu/vm/% $(USR_PSM_IVM_DIR)
	$(INS.file)

$(ROOTDIRS):
	$(INS.dir.bin.bin)

# -r because this used to be a directory and is now a link.
$(ROOTLINK):	$(ROOTDIRS)
	-$(RM) -r $@; $(SYMLINK) $(LINKDEST) $@ $(CHOWNLINK) $(CHGRPLINK)

%.check:	../../sunmmu/vm/%.h
	$(DOT_H_CHECK)

include ../../Makefile.targ
