#
#ident	"@(#)Makefile.com	1.8	95/03/03 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#

LIBRARY=	libld.a
VERS=		.2

MACHOBJS= 	_entry.o	machrel.o

COMOBJS=	entry.o		files.o		globals.o	libs.o \
		outfile.o	place.o		relocate.o	resolve.o \
		sections.o	support.o	syms.o		update.o \
		util.o		version.o

OBJECTS=	$(MACHOBJS) $(GENLDOBJS) $(COMOBJS)

MAPFILE=	../common/mapfile

include 	../../../../lib/Makefile.lib

SGSPROTO=	../../proto/$(MACH)
CPPFLAGS=	-DPIC -D_TS_ERRNO -I../common -I../../include \
		-I../../include/$(MACH) $(CPPFLAGS.master)
DYNFLAGS +=	-L ../../liblddbg/$(MACH) -L ../../libconv/$(MACH)
LLDLIBS=	-llddbg
LDLIBS +=	-lelf -lconv $(LLDLIBS)
LINTFLAGS +=	-L ../../liblddbg/$(MACH) -L ../../libconv/$(MACH) $(LDLIBS)


# A bug in pmake causes redundancy when '+=' is conditionally assigned, so
# '=' is used with extra variables.
# $(DYNLIB) :=	DYNFLAGS += -Yl,$(SGSPROTO) -M $(MAPFILE)
#
XXXFLAGS=
$(DYNLIB) :=	XXXFLAGS= -Yl,$(SGSPROTO) -M $(MAPFILE)
DYNFLAGS +=	$(XXXFLAGS)


SRCS=		$(MACHOBJS:%.o=%.c) $(COMOBJS:%.o=../common/%.c)

CLEANFILES +=	$(LINTOUT)
CLOBBERFILES +=	$(DYNLIB)  $(LINTLIB)  $(LIBLINKS)

ROOTDYNLIB=	$(DYNLIB:%=$(ROOTLIBDIR)/%)
ROOTLINTLIB=	$(LINTLIB:%=$(ROOTLIBDIR)/%)
