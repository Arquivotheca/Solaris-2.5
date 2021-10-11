#
#ident	"@(#)Makefile.com	1.6	94/09/13 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG=		ld

include 	../../../Makefile.cmd

SGSPROTO=	../../proto/$(MACH)

COMOBJS=	main.o		args.o		entry.o		globals.o \
		libs.o		util.o		map.o		debug.o

OBJS =		$(MACHOBJS) $(COMOBJS)
.PARALLEL:	$(OBJS)

MAPFILE=	../common/mapfile

CPPFLAGS=	-I../common -I../../include -I../../include/$(MACH) \
		$(CPPFLAGS.master)
LDFLAGS +=	-Yl,$(SGSPROTO) -M $(MAPFILE)
LLDLIBS=	-L ../../libld/$(MACH) -lld -lelf -lc -ldl \
		-L ../../liblddbg/$(MACH) -llddbg
LDLIBS +=	$(LLDLIBS)
LINTFLAGS +=	$(LDLIBS)
CLEANFILES +=	$(LINTOUT)

SRCS=		$(MACHOBJS:%.o=%.c)  $(COMOBJS:%.o=../common/%.c)

ROOTCCSBIN=	$(ROOT)/usr/ccs/bin
ROOTCCSBINPROG=	$(PROG:%=$(ROOTCCSBIN)/%)

FILEMODE=	0755
