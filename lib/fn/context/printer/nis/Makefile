#
#ident	"@(#)Makefile	1.4	94/11/24 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# lib/fn/context/printer/nis/Makefile
#

LIBRARYCCC= fn_ctx_onc_fn_printer_nis.a
VERS = .1

PRINTER_OBJECTS = FNSP_nisPrinternameContext.o

COMMONOBJS = FNSP_printer_Syntax.o

FILESOBJS = FNSP_PrinternameContext.o

OBJECTS = onc_fn_printer_nis.o $(PRINTER_OBJECTS) $(COMMONOBJS) $(FILESOBJS)

# include library definitions
include ../../../Makefile.libfn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lfn_spf -lfn_p -lnsl -lC -lc 

debug :=	CPPFLAGS += -g
tcov :=		CPPFLAGS += -a

.KEEP_STATE:

all: $(LIBS)

debug install tcov: $(ROOTLIBDIR) $(ROOTLINKSCCC)

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# all FILEOBJS and COMMONOBJS are located in nisplus directory
# to get around C++ problem with dlopen
objs/%.o profs/%.o pics/%.o: ../nisplus/%.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

# include library targets
include ../../../Makefile.targ
