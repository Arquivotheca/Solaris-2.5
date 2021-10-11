#
#ident	"@(#)Makefile	1.10	95/03/28 SMI"
#
# Copyright (c) 1989-1993, by Sun Microsystems, Inc.
#
# uts/sparc/v7/sys/Makefile
#
# include global definitions
include ../../../../Makefile.master

HDRS=  \
	machpcb.h	\
	machtrap.h	\
	mutex_impl.h	\
	privregs.h	\
	prom_isa.h	\
	psr.h		\
	traptrace.h

ROOTINCV7= $(ROOT)/usr/include/v7
ROOTDIR= $(ROOTINCV7)/sys
ROOTDIRS= $(ROOTDIR)

ROOTHDRS= $(HDRS:%=$(ROOTDIR)/%)

CHECKHDRS= $(HDRS:%.h=%.check)

# install rules
$(ROOTDIR)/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS)

install_h: $(ROOTINCV7) .WAIT $(ROOTDIRS) .WAIT $(ROOTHDRS) 

$(ROOTINCV7):
	$(INS.dir)

$(ROOTDIRS):	$(ROOTINCV7)
	$(INS.dir)

check:	$(CHECKHDRS)
