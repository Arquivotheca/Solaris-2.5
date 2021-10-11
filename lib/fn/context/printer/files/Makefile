#
#ident	"@(#)Makefile	1.4	94/11/24 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# lib/fn/context/printer/files/Makefile
#

LIBRARYCCC= fn_ctx_onc_fn_printer_files.a
VERS = .1

PRINTER_OBJECTS = FNSP_PrinternameContext.o

COMMONOBJS = FNSP_printer_Syntax.o

OBJECTS = onc_fn_printer_files.o $(PRINTER_OBJECTS) $(COMMONOBJS)

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

# PRINTER_OBJS and COMMONOBJS are located in nisplus directory
# to get around C++ problem with dlopen
objs/%.o profs/%.o pics/%.o: ../nisplus/%.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

# include library targets
include ../../../Makefile.targ
