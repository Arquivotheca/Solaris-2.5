#
#ident	"@(#)Makefile	1.11	93/03/29 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# uts/common/net/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	af.h	  if.h	    if_arp.h	  route.h

ROOTDIRS= $(ROOT)/usr/include/net

ROOTHDRS= $(HDRS:%=$(ROOT)/usr/include/net/%)

CHECKHDRS= $(HDRS:%.h=%.check)

$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(ROOTDIRS) $(ROOTHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
