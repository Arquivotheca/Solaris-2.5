#
#ident	"@(#)Makefile.com	1.1	94/08/30 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# cmd/sgs/dump/Makefile.com

PROG=		dump

include 	../../../Makefile.cmd

COMOBJS=	debug.o		dump.o		fcns.o
DEMOBJS=	dem.o		cafe_dem.o

SRCS=		$(COMOBJS:%.o=../common/%.c) \
		$(DEMOBJS:%.o=../../c++/demangler/common/%.c)

OBJS =		$(COMOBJS)  $(DEMOBJS)
.PARALLEL:	$(OBJS)

CPPFLAGS=	-I../common -I../../include -I../../include/$(MACH) \
		$(CPPFLAGS.master)
LDLIBS +=	-lelf
LINTFLAGS +=	$(LDLIBS)
CLEANFILES +=	$(LINTOUT)

$(DEMOBJS):=    CPPFLAGS = -I../../c++/demangler/common -DELF_OBJ \
		$(CPPFLAGS.master)
