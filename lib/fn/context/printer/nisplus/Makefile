#
#ident	"@(#)Makefile	1.5	95/01/29 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# lib/fn/context/printer/nisplus/Makefile
#

LIBRARYCCC= fn_ctx_onc_fn_printer_nisplus.a
VERS = .1

PRINTER_OBJECTS = FNSP_nisplusPrinternameContext.o FNSP_nisplusPrinterObject.o

FILESOBJS = FNSP_PrinternameContext.o

COMMONOBJS = FNSP_printer_Address.o FNSP_printer_Syntax.o

OBJECTS = onc_fn_printer_nisplus.o fnsp_printer_internal.o fnsp_nisplus_root.o \
	$(PRINTER_OBJECTS) $(COMMONOBJS) $(FILESOBJS)

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

# for fnsp_nisplus_root.o
objs/%.o profs/%.o pics/%.o: ../../onc_ns/nisplus/%.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)


# include library targets
include ../../../Makefile.targ
