#
#ident	"@(#)Makefile	1.1	89/12/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# uts/common/c2/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	audit.h audit_kernel.h audit_kevents.h audit_record.h

ROOTDIRS= $(ROOT)/usr/include/bsm

ROOTHDRS= $(HDRS:%=$(ROOTDIRS)/%)

CHECKHDRS=	$(HDRS:%.h=%.check)

# install rule
$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

install_h: $(ROOTDIRS) $(ROOTHDRS)

$(ROOTDIRS):
	$(INS.dir)

check: $(CHECKHDRS)
