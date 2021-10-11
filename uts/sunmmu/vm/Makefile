#
#ident	"@(#)Makefile	1.5	93/03/29 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# uts/sunmmu/vm/Makefile
#
# Note that this sun-mmu specific file knows the machines which have this
# style of mmu. It would be better if this were not the case, but this will
# be fixed by the generic kernel project so short term improvements are
# not worth it.
#
# include global definitions

include ../../../Makefile.master

HDRS=		hat_sunm.h

ROOTDIR=	$(ROOT)/usr/share/src
ROOTDIRS=	$(ROOTDIR)/uts \
		$(ROOTDIR)/uts/sun4 $(ROOTDIR)/uts/sun4c $(ROOTDIR)/uts/sun4e \
		$(ROOTDIR)/uts/sun4/vm $(ROOTDIR)/uts/sun4c/vm \
		$(ROOTDIR)/uts/sun4e/vm

ROOTHDRS4=	$(HDRS:%=$(ROOTDIR)/uts/sun4/vm/%)
ROOTHDRS4C=	$(HDRS:%=$(ROOTDIR)/uts/sun4c/vm/%)
ROOTHDRS4E=	$(HDRS:%=$(ROOTDIR)/uts/sun4e/vm/%)

CHECKHDRS=	$(HDRS:%.h=%.check)

# install rules
$(ROOTDIR)/uts/sun4/vm/%: %
	$(INS.file)

$(ROOTDIR)/uts/sun4c/vm/%: %
	$(INS.file)

$(ROOTDIR)/uts/sun4e/vm/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS)

install_h: $(ROOTDIRS) $(ROOTHDRS4) $(ROOTHDRS4C) $(ROOTHDRS4E)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
