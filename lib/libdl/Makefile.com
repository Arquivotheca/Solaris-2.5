#
#ident	"@(#)Makefile.com	1.3	95/03/31 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libdl/Makefile.com

LIBRARY=	libdl.a
VERS=		.1

COMOBJS=	dl.o
OBJECTS=	$(COMOBJS)

include 	../../../lib/Makefile.lib

DYNFLAGS +=	-F/usr/lib/ld.so.1 -M$(MAPFILE-FLTR)

# Redefine shared object build rule to use $(LD) directly (this avoids .init
# and .fini sections being added).  Because we use a mapfile to create a
# single text segment, hide the warning from ld(1) regarding a zero _edata.

BUILD.SO=	$(LD) -o $@ -G $(DYNFLAGS) $(PICS) $(LDLIBS) 2>&1 | \
		fgrep -v "No read-write segments found" | cat

SRCS=		$(COMOBJS:%.o=../common/%.c)

CLEANFILES +=	$(LINTOUT)
CLOBBERFILES +=	$(DYNLIB)  $(LINTLIB)  $(LIBLINKS)

ROOTDYNLIB=	$(DYNLIB:%=$(ROOTLIBDIR)/%)
ROOTLINTLIB=	$(LINTLIB:%=$(ROOTLIBDIR)/%)

# A version of this library needs to be placed in /etc/lib to allow
# dlopen() functionality while in single-user mode.
ETCLIBDIR=	$(ROOT)/etc/lib
ETCDYNLIB=	$(DYNLIB:%=$(ETCLIBDIR)/%)

$(ETCDYNLIB) :=	FILEMODE= 755
