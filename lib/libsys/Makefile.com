#
#ident	"@(#)Makefile.com	1.2	95/03/31 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libsys/Makefile.com

LIBRARY=	libsys.a
VERS=		.1

COMOBJ=		libsys.o
OBJECTS=	$(COMOBJ)  $(MACHOBJ)

include 	../../../lib/Makefile.lib

# Define libsys to be a filter on libc.  The ABI requires the runtime linker as
# the soname.

DYNFLAGS +=	-F/usr/lib/libc.so.1 -M$(MAPFILE-FLTR)
SONAME=		/usr/lib/ld.so.1

# Redefine shared object build rule to use $(LD) directly (this avoids .init
# and .fini sections being added).  Because we use a mapfile to create a
# single text segment, hide the warning from ld(1) regarding a zero _edata.

BUILD.SO=	$(LD) -o $@ -G $(DYNFLAGS) $(PICS) $(LDLIBS) 2>&1 | \
		fgrep -v "No read-write segments found" | cat

pics/%.o :=	ASFLAGS += -K pic

COMSRC=		$(COMOBJ:%.o=%.c)
MACHSRC=	$(MACHOBJ:%.o=%.s)

CLOBBERFILES +=	$(DYNLIB)  $(LIBLINKS)  $(COMSRC)  $(MACHSRC)
