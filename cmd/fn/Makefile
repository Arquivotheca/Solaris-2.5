#
#ident	"@(#)Makefile	1.19	95/07/16 SMI"
#
# Copyright (c) 1989 - 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# Makefile for the fn utilities
# cmd/fn/Makefile
#

PROG = fnbind fnunbind fnlookup fnlist fnrename fnattr fncreate_printer
SHFILE = fncheck fnsetup

SBINPROG = fncreate fndestroy fncreate_fs fncheck #fnsetup

include ../Makefile.cmd

ROOTPROGS	= $(PROG:%=$(ROOTBIN)/%)

ROOTSBINPROGS	= $(SBINPROG:%=$(ROOTUSRSBIN)/%)

SHSRCS=		$(SHFILE:%=%.sh)

LDLIBS += -lxfn

# CC adds /opt/SUNWspro/lib to the run_path it passes to ld.  This overrides
# it.
RUNPATH = -R/usr/lib
fncreate fncreate_fs fncreate_printer := RUNPATH = -R/usr/lib/fn
$(PROG) $(SBINPROG) := LDFLAGS += -Qoption ld $(RUNPATH)

# The following are for fncreate only
HLIBS = -lfn_p -lnsl $(ROOTLIB)/fn/fn_ctx_onc_fn_nisplus.so.1
NHDRS1 = $(SRC)/lib/fn/context/onc_ns
NHDRS2 = $(SRC)/lib/fn/context/onc_ns/nisplus

# The following are for fncreate_printer only
PLIBS = -lintl -lfn_p -lnsl $(ROOTLIB)/fn/fn_ctx_onc_fn_printer_nisplus.so.1
PHDRS1 = $(SRC)/lib/fn/context/printer
PHDRS2 = $(SRC)/lib/fn/context/printer/nisplus

# The following are for fncreate_fs only
FSOBJS = fncreate_fs.o fs_parse_utils.o fs_ctx_utils.o Tree.o
FSLIBS = -lnsl -lintl $(ROOTLIB)/fn/fn_ctx_onc_fn_nisplus.so.1
FSHDRS1= $(SRC)/lib/fn/libxfn
FSHDRS2= $(SRC)/lib/fn/context/onc_ns/nisplus

FNSHDRS = $(SRC)/lib/fn/include

CPPFLAGS += -I$(FNSHDRS)

debug :=	CPPFLAGS += $(DEBUG)
debug :=	COPTFLAG = -g
debug :=	CCOPTFLAG = -g
tcov :=		CFLAGS += -xa
tcov :=		CCFLAGS += -a
fncreate :=	CPPFLAGS += -I$(NHDRS1) -I$(NHDRS2) 
$(FSOBJS) :=	CPPFLAGS += -I$(FSHDRS1) -I$(FSHDRS2)
fncreate_printer :=	CPPFLAGS += -I$(PHDRS1) -I$(PHDRS2) 
#tcov :=		CPPFLAGS += -a
#debug :=		CPPFLAGS += -g

.KEEP_STATE:

all: $(PROG) $(SBINPROG)

debug install: all $(SHFILE) $(ROOTBIN) $(ROOTUSRSBIN) $(ROOTPROG) \
	$(ROOTSBINPROGS)

install_h:

%.o: %.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

fncreate: fncreate.o fnutils.o
	$(LINK.cc) -o $@ fncreate.o fnutils.o $(LDLIBS) $(HLIBS)

fncreate_printer: fncreate_printer.o
	$(LINK.cc) -o $@ fncreate_printer.o $(LDLIBS) $(PLIBS)

fncreate_fs: $(FSOBJS)
	$(LINK.cc) -o $@ $(FSOBJS) $(LDLIBS) $(FSLIBS)

fndestroy: fndestroy.o
	$(LINK.cc) -o $@ fndestroy.o $(LDLIBS)

fnbind: fnbind.o
	$(LINK.cc) -o $@ fnbind.o $(LDLIBS) -lnsl

fnunbind: fnunbind.o
	$(LINK.cc) -o $@ fnunbind.o $(LDLIBS)

fnlookup: fnlookup.o
	$(LINK.cc) -o $@ fnlookup.o $(LDLIBS)

fnlist: fnlist.o
	$(LINK.cc) -o $@ fnlist.o $(LDLIBS)

fnattr: fnattr.o 
	$(LINK.cc) -o $@ fnattr.o $(LDLIBS)

fnrename: fnrename.o 
	$(LINK.cc) -o $@ fnrename.o $(LDLIBS)

$(ROOTBIN) $(ROOTUSRSBIN):
	$(INS.dir)

clean:
	-$(RM) $(PROG:%=%.o) fnutils.o
	-$(RM) $(PROG) $(SBINPROG) $(SHFILE)

# disable linting since it is useless for C++ code
lint:

include ../Makefile.targ
