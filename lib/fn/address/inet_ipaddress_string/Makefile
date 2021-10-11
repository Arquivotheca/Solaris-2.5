#
#ident	"@(#)Makefile	1.4	94/09/08 SMI"
#
# Copyright (c) 1989 - 1994 by Sun Microsystems, Inc.
#
# lib/fn/address/inet_ipaddress_string/Makefile
#

LIBRARYCCC= fn_ref_addr_inet_ipaddr_string.a
VERS = .1

OBJECTS= inet_ipaddr_string.o

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
