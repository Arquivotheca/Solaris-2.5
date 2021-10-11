#
#ident	"@(#)Makefile	1.18	95/02/09 SMI"
#
# Copyright (c) 1989,1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# uts/common/nfs/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	export.h	lm.h		nfs.h		nfssys.h \
	mount.h		nfs_acl.h	nfs_clnt.h	rnode.h \
	lm_nlm.h	lm_server.h

ROOTDIRS= $(ROOT)/usr/include/nfs

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
