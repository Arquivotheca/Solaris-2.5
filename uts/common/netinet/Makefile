#
#ident	"@(#)Makefile	1.13	93/03/29 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# uts/common/netinet/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=	arp.h \
	icmp_var.h	if_ether.h	igmp.h		igmp_var.h \
	in.h		in_pcb.h	in_systm.h	in_var.h \
	ip.h		ip_icmp.h	ip_mroute.h	ip_var.h \
	tcp.h		tcp_debug.h	tcp_fsm.h	tcp_seq.h \
	tcp_timer.h	tcp_var.h	tcpip.h		udp.h \
	udp_var.h


ROOTDIRS= $(ROOT)/usr/include/netinet

ROOTHDRS= $(HDRS:%=$(ROOT)/usr/include/netinet/%)

CHECKHDRS= $(HDRS:%.h=%.check)

$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(ROOTDIRS) $(ROOTHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
