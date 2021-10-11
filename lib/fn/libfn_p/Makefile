#
#ident	"@(#)Makefile	1.10	94/11/20 SMI"
#
# Copyright (c) 1989 - 1994 by Sun Microsystems, Inc.
#
# lib/fn/libfn_p/Makefile
#

LIBRARYCCC= libfn_p.a
VERS = .1

HHDRS= fn_p.hh fn_printer_p.hh
FNHHDRS= $(HHDRS:%=$(FNINCDIR)/xfn/%)

OBJECTS= fnsp_reference.o

# include library definitions
include ../Makefile.libfn

ROOTLIBDIR=	$(ROOT)/usr/lib

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lnsl -lC -lc

.KEEP_STATE:

all: private_h $(LIBS)

debug install tcov: private_h $(ROOTLINKSCCC)

install_h check:

private_h: $(FNINCDIR) $(FNINCDIR)/xfn $(FNHHDRS)

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../Makefile.targ
