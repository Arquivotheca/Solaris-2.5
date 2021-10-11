#
#ident "@(#)Makefile	1.2 94/11/08 SMI"
#
# Copyright (c) 1989 - 1994 by Sun Microsystems, Inc.
#
# lib/fn/address/onc_fn_nislus/Makefile
#

LIBRARYCCC= fn_ref_addr_onc_fn_nis.a
VERS = .1

OBJECTS= onc_fn_nis.o

# include library definitions
include ../../Makefile.libfn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lfn_p -lC -lc

.KEEP_STATE:

all: $(LIBS) 

lib: $(LIBRARY)

debug install tcov: $(ROOTLIBDIR) $(ROOTLINKSCCC)

install_h:

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../../Makefile.targ
