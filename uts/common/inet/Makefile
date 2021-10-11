#
#ident	"@(#)Makefile	1.6	93/03/29 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# uts/common/inet/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	arp.h	ip.h	ip_multi.h	mi.h	common.h	led.h	nd.h \
	mib2.h	optcom.h	snmpcom.h	tcp.h	ip_if.h	ip_ire.h



ROOTDIRS= $(ROOT)/usr/include/inet

ROOTHDRS= $(HDRS:%=$(ROOT)/usr/include/inet/%)

CHECKHDRS= $(HDRS:%.h=%.check)

$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(ROOTDIRS) $(ROOTHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
