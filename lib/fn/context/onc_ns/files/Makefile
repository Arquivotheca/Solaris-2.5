#
#ident	"@(#)Makefile	1.4	94/11/24 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/context/onc_ns/files/Makefile
#

LIBRARYCCC= fn_ctx_onc_fn_files.a
VERS = .1

FILESOBJECTS = FNSP_OrgContext_impl.o FNSP_FlatContext_impl.o

COMMONOBJS = FNSP_Syntax.o FNSP_Address.o

OBJECTS = onc_fn_files.o $(FILESOBJECTS) $(COMMONOBJS)

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

# include library targets
include ../../../Makefile.targ
