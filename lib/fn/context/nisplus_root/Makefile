#
#ident	"@(#)Makefile	1.1	95/01/29 SMI"
#
# Copyright (c) 1989 - 1995 by Sun Microsystems, Inc.
#
# lib/fn/context/nisplus_root/Makefile
#

LIBRARYCCC= fn_ctx_onc_fn_nisplus_root.a
VERS = .1

OBJECTS = from_ref.o nisplus_cache.o fnsp_nisplus_root.o

# include library definitions
include ../../Makefile.libfn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lfn_spf -lnsl -lfn_p -lC -lc

CPPFLAGS += -I../../../libnsl/nis/cache

debug :=	CPPFLAGS += -g
tcov :=		CPPFLAGS += -a

# for testing
HDIR3 = $(SRC)/lib/fn/include
LDLIBS += -lxfn
CPPFLAGS += -I$(HDIR3)

.KEEP_STATE:

all: $(LIBS)

debug install tcov: $(ROOTLIBDIR) $(ROOTLINKSCCC)

install_h:

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse


# for fnsp_nisplus_root.o
objs/%.o profs/%.o pics/%.o: ../onc_ns/nisplus/%.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

# include library targets
include ../../Makefile.targ

