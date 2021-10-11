#
#ident	"@(#)Makefile	1.3 94/08/11 SMI"
#
# Copyright (c) 1989 - 1994 by Sun Microsystems, Inc.
#
# lib/fn/address/onc_fn_nislus/Makefile
#

LIBRARYCCC= fn_ref_addr_fn_link_addr.a
VERS = .1

OBJECTS= fn_link_addr.o

# include library definitions
include ../../Makefile.libfn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lC -lc

.KEEP_STATE:

all: $(LIBS) 

lib: $(LIBRARY)

debug install tcov: $(ROOTLIBDIR) $(ROOTLINKSCCC)

install_h:

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../../Makefile.targ
