#
#ident	"@(#)Makefile.com	1.13	94/10/03 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libelf/Makefile.com
#

LIBRARY=	libelf.a
VERS=		.1
M4=		m4

MACHOBJS=	foreign.o

COMOBJS=	ar.o		begin.o		cntl.o		cook.o \
		data.o		end.o		fill.o		flag.o \
		getarhdr.o	getarsym.o	getbase.o	getdata.o \
		getehdr.o	getident.o	getphdr.o	getscn.o \
		getshdr.o	hash.o		input.o		kind.o \
		ndxscn.o	newdata.o	newehdr.o	newphdr.o \
		newscn.o	next.o		nextscn.o	output.o \
		rand.o		rawdata.o	rawfile.o	rawput.o \
		strptr.o	update.o

BLTSRCOBJS=	error.o		xlate.o

MISCOBJS=	String.o	args.o		demangle.o	nlist.o \
		nplist.o

OBJECTS=	$(MACHOBJS)  $(COMOBJS)  $(BLTSRCOBJS)  $(MISCOBJS)

include		../../Makefile.lib

CPPFLAGS=	-I../common $(CPPFLAGS.master)

BUILD.AR=	$(RM) $@ ; \
		$(AR) q $@ `$(LORDER) $(OBJECTS:%=$(DIR)/%)| $(TSORT)`
		$(POST_PROCESS_A)

BLTSRCS=	../common/error.c ../common/error.h ../common/xlate.c
SRCS=		$(COMOBJS:%.o=../common/%.c)  $(MISCOBJS:%.o=../misc/%.c) \
		$(MACHOBJS:.o=.c)  $(BLTSRCS)

CLEANFILES +=	$(LINTOUT)  $(BLTSRCS)

LIBS +=		$(DYNLIB) $(LINTLIB)
