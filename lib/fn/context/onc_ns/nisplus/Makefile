#
#ident	"@(#)Makefile	1.10	95/01/29 SMI"
#
# Copyright (c) 1989 - 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/context/onc_ns/nisplus/Makefile
#

LIBRARYCCC= fn_ctx_onc_fn_nisplus.a
VERS = .1

NISPLUSOBJS = FNSP_WeakSlashContext.o FNSP_HierContext.o \
	FNSP_FlatContext.o FNSP_DotContext.o \
	FNSP_HUContext.o FNSP_OrgContext.o \
	FNSP_HostnameContext.o FNSP_UsernameContext.o \
	FNSP_ENSContext.o

COMMONOBJS = FNSP_NullContext.o FNSP_Syntax.o FNSP_Address.o

OBJECTS = fnsp_internal.o fnsp_nisplus_root.o onc_fn_nisplus.o FNSP_Context.o \
	$(NISPLUSOBJS) $(COMMONOBJS)

# include library definitions
include ../../../Makefile.libfn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lfn_spf -lnsl -lfn_p -lC -lc

debug :=	CPPFLAGS += -g
tcov :=		CPPFLAGS += -a

.KEEP_STATE:

all: $(LIBS) 

debug install tcov: $(ROOTLIBDIR) $(ROOTLINKSCCC)

install_h:

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../../../Makefile.targ
