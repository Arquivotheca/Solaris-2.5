#
#ident	"@(#)Makefile	1.3	95/03/28 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/context/x500/Makefile
#

LIBRARYCCC = fn_ctx_x500.a
VERS = .1
OBJECTS = x500.o x500context.o x500dua.o x500dua2.o x500utils.o
TOUCH = touch

# include library definitions
include ../../Makefile.libfn

# do after include Makefile.lib, which also sets ROOTLIBDIR
ROOTLIBDIR=    $(ROOT)/usr/lib/fn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lfn_spf libxomxds.so -lC -lc

$(DYNLIBCCC) := DYNFLAGS = -h $(SONAMECCC) -Qoption ld -R/opt/SUNWxds/lib

CPPFLAGS += -I.

.KEEP_STATE:


all: libxomxds.so $(LIBS)

# create a dummy shared object for the linker
# (real shared object will be supplied in /opt/SUNWxds/lib/libxomxds.so)
pics/xomxds.o:
	$(TOUCH) xomxds.cc
	$(CCC) -c xomxds.cc -o $@ 
	$(RM) xomxds.cc

libxomxds.so: pics .WAIT pics/xomxds.o
	$(CCC) -o $@ -h $@ -G pics/xomxds.o
	$(POST_PROCESS_SO)

debug install tcov: all $(ROOTLIBDIR) $(ROOTLINKSCCC)

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../../Makefile.targ

