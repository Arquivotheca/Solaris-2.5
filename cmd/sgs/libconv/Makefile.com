#
#ident	"@(#)Makefile.com	1.2	95/03/03 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

LIBRARY=	libconv.a

include 	../../../../Makefile.master


MACHOBJS= 	doreloc.o	relocate.o

COMOBJS=	data.o		deftag.o	dl.o		dynamic.o \
		elf.o		globals.o 	phdr.o		sections.o \
		segments.o	symbols.o	version.o

OBJECTS=	$(MACHOBJS) $(COMOBJS)
PICS=		$(OBJECTS:%=pics/%)

CPPFLAGS=	-DPIC -I../common -I../../include -I../../include/$(MACH) \
		$(CPPFLAGS.master)
CFLAGS +=	-K pic
ARFLAGS=	r

SRCS=		$(MACHOBJS:%.o=%.c)  $(COMOBJS:%.o=../common/%.c)

LIBNAME=	$(LIBRARY:lib%.a=%)
LINTOUT=	lint.out
CLEANFILES +=	$(LINTOUT)
CLOBBERFILES +=	$(LINTLIB)
LINTLIB=	llib-l$(LIBNAME).ln
LINTFLAGS=	-uax $(CPPFLAGS)
