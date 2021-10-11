#
#ident	"@(#)Makefile	1.4	94/11/24 SMI"
#
# Copyright (c) 1984 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/context/onc_ns/nis/Makefile
#

LIBRARYCCC= fn_ctx_onc_fn_nis.a
VERS = .1

COMMONOBJS= FNSP_Address.o FNSP_Syntax.o

FILESOBJS = FNSP_OrgContext_impl.o FNSP_FlatContext_impl.o

OBJECTS = onc_fn_nis.o $(COMMONOBJS) $(FILESOBJS)

# include library definitions
include ../../../Makefile.libfn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lfn_spf -lnsl -lfn_p -lC -lc

debug :=	CPPFLAGS += -g
tcov :=		CPPFLAGS += -a

.KEEP_STATE:

all: $(LIBS)

debug install tcov: $(ROOTLIBDIR) $(ROOTLINKSCCC)

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# for COMMONOBJS
objs/%.o profs/%.o pics/%.o: ../nisplus/%.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

# for FILEOBJS
objs/%.o profs/%.o pics/%.o: ../files/%.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

# include library targets
include ../../../Makefile.targ
