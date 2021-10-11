#
#ident	"@(#)Makefile.com	1.6	95/03/03 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

RTLD=		ld.so.1

OBJECTS=	$(P_ASOBJS)   $(P_COMOBJS)   $(P_MACHOBJS) \
		$(S_ASOBJS)   $(S_COMOBJS)   $(S_MACHOBJS)

COMOBJS=	$(P_COMOBJS)  $(S_COMOBJS)
ASOBJS=		$(P_ASOBJS)   $(S_ASOBJS)
MACHOBJS=	$(P_MACHOBJS) $(S_MACHOBJS)

MAPFILE=	../common/mapfile

include		../../../../lib/Makefile.lib

# A version of this library needs to be placed in /etc/lib to allow
# dlopen() functionality while in single-user mode.

ETCLIBDIR=	$(ROOT)/etc/lib
ETCDYNLIB=	$(RTLD:%=$(ETCLIBDIR)/%)

ROOTDYNLIB=	$(RTLD:%=$(ROOTLIBDIR)/%)

FILEMODE =	755

# Add -DPRF_RTLD to allow ld.so.1 to profile itself

SGSPROTO=	../../proto/$(MACH)
INCLIST=	-I. -I../common -I../../include -I../../include/$(MACH)
CPPFLAGS=	$(INCLIST) $(CPPFLAGS.master)
ASFLAGS=	-P -D_ASM $(CPPFLAGS)
SONAME=		/usr/lib/$(RTLD)
DYNFLAGS +=	-e _rt_boot -Bsymbolic -zdefs -M $(MAPFILE)
LDLIBS=		$(LDLIBS.lib) -L$(ROOT)/usr/lib/pics -lc_pic \
		-L ../../libld/$(MACH) -lld -lelf -lc \
		-L ../../liblddbg/$(MACH) -llddbg \
		-L ../../libconv/$(MACH) -lconv
BUILD.s=	$(AS) $(ASFLAGS) $< -o $@
LD=		$(SGSPROTO)/ld

# list of sources for lint
lint:=		LDLIBS = $(LDLIBS.lib) -lc -lelf \
		-L ../../libld/$(MACH) -lld -L ../../liblddbg/$(MACH) -llddbg

SRCS=		$(COMOBJS:%.o=../common/%.c)  $(MACHOBJS:.o=.c) \
		$(ASOBJS:.o=.s)
LINTFLAGS=	-ax -Dsun $(LDLIBS)

CLEANFILES +=	$(LINTOUT)
CLOBBERFILES +=	$(RTLD)
