#
#ident	"@(#)Makefile	1.2	94/12/05 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/address/x500/Makefile
#

LIBRARYCCC= fn_ref_addr_x500.a
VERS = .1

OBJECTS= x500.o

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
