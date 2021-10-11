#
#ident	"@(#)Makefile	1.3	93/03/29 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# uts/common/kerberos/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	des.h		krb.h		mit-copyright.h

ROOTDIRS= $(ROOT)/usr/include/kerberos

ROOTHDRS= $(HDRS:%=$(ROOTDIRS)/%)

CHECKHDRS= $(HDRS:%.h=%.check)

$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(ROOTDIRS) $(ROOTHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
