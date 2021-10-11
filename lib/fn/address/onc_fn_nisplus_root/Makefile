#
#ident	"@(#)Makefile	1.2	95/01/31 SMI"
#
# Copyright (c) 1989 - 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/address/onc_fn_nisplus_root/Makefile
#
#
# produces fn_inet_nisplus.so (a symbolic link) as well

LIBRARYCCC= fn_ref_addr_onc_fn_nisplus_root.a
VERS = .1

OBJECTS= description.o to_ref.o

# include library definitions
include ../../Makefile.libfn


LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lnsl -lC -lc 

.KEEP_STATE:

all: $(LIBS) 

lib: $(LIBRARY)

install debug tcov: $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLINKSCCC) install_symlink

install_h:

install_symlink: $(FNLIBDIR) $(DYNLIBCCC)
	$(RM) $(FNLIBDIR)/fn_inet_nisplus.so
	$(SYMLINK) $(DYNLIBCCC) $(FNLIBDIR)/fn_inet_nisplus.so

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../../Makefile.targ
