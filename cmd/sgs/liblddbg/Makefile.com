#
#	Copyright (c) 1994 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile.com	1.5	94/10/05 SMI"
#

LIBRARY=	liblddbg.a
VERS=		.3

MACHOBJS=	_relocate.o

COMOBJS=	args.o		bindings.o	debug.o	\
		dynamic.o	entry.o		elf.o		files.o \
		libs.o		map.o		note.o		phdr.o \
		relocate.o	sections.o	segments.o	shdr.o \
		support.o	syms.o		util.o		version.o

OBJECTS=	$(MACHOBJS)  $(COMOBJS)

MAPFILE=	../common/mapfile

include		../../../../lib/Makefile.lib

SGSPROTO=	../../proto/$(MACH)
CPPFLAGS=	-DPIC -I. -I../common -I../../include/$(MACH) \
		-I../../include $(CPPFLAGS.master)
DYNFLAGS +=	-L../../libconv/$(MACH)
LDLIBS=		-lconv
LINTFLAGS +=	-L ../../libconv/$(MACH) $(LDLIBS)


# A bug in pmake causes redundancy when '+=' is conditionally assigned, so
# '=' is used with extra variables.
# $(DYNLIB) :=  DYNFLAGS += -Yl,$(SGSPROTO) -M $(MAPFILE)
#
XXXFLAGS=
$(DYNLIB) :=    XXXFLAGS= -Yl,$(SGSPROTO) -M $(MAPFILE)
DYNFLAGS +=     $(XXXFLAGS)


SRCS=		$(MACHOBJS:%.o=%.c)  $(COMOBJS:%.o=../common/%.c)

CLEANFILES +=	$(LINTOUT)
CLOBBERFILES +=	$(DYNLIB)  $(LINTLIB) $(LIBLINKS)

ROOTDYNLIB=	$(DYNLIB:%=$(ROOTLIBDIR)/%)
ROOTLINTLIB=	$(LINTLIB:%=$(ROOTLIBDIR)/%)
