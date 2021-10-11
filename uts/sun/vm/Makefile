#
#ident	"@(#)Makefile	1.6	92/07/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# uts/sun/vm/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	seg_kmem.h


ROOTDIRS= $(ROOT)/usr/include/vm

ROOTHDRS= $(HDRS:%=$(ROOTDIRS)/%)

# install rule
$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

install_h: $(ROOTDIRS) $(ROOTHDRS)

$(ROOTDIRS):
	$(INS.dir)
