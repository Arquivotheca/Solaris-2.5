#
#ident	"@(#)Makefile	1.9	93/03/29 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# uts/common/des/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	des.h		desdata.h	softdes.h

ROOTDIRS= $(ROOT)/usr/include/des

ROOTHDRS= $(HDRS:%=$(ROOTDIRS)/%)

CHECKHDRS= $(HDRS:%.h=%.check)

# install rule
$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(ROOTDIRS) $(ROOTHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
