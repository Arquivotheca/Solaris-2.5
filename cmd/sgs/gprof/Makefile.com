#
#ident	"@(#)Makefile.com	1.4	94/09/20 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/gprof/sparc/Makefile
#

include 	../../../Makefile.cmd

COMOBJS=	gprof.o arcs.o dfn.o lookup.o calls.o hertz.o \
		printgprof.o printlist.o readelf.o
WHATOBJS=	whatdir.o
DEMOBJS=	dem.o cafe_dem.o

OBJS=		$(COMOBJS) $(WHATOBJS) $(DEMOBJS)
BLURBS=		gprof.callg.blurb gprof.flat.blurb
SRCS=		$(COMOBJS:%.o=../common/%.c) \
		$(WHATOBJS:%.o=../../whatdir/common/%.c) \
		$(DEMOBJS:%.o=../../c++/demangler/common/%.c)

INCLIST=	-I../common -I../../include -I../../include/$(MACH)
DEFLIST=	-DELF_OBJ -DELF -DSPARC
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LINTFLAGS +=	-n $(LDLIBS)
CLEANFILES +=	$(LINTOUT)

$(DEMOBJS):=	INCLIST = -I../../c++/demangler/common
$(DEMOBJS):=	DEFLIST = -DELF_OBJ

ROOTCCSBLURB=	$(BLURBS:%=$(ROOTCCSBIN)/%)

$(ROOTCCSBLURB) :=	FILEMODE=	444

$(ROOTCCSBIN)/%: ../common/%
		$(INS.file)

%.o:		../common/%.c
		$(COMPILE.c) $<

%.o:		../../whatdir/common/%.c
		$(COMPILE.c) $<

%.o:		../../c++/demangler/common/%.y
		$(YACC.y) $<
		$(COMPILE.c) -o $@ y.tab.c
		$(RM) y.tab.c

%.o:		../../c++/demangler/common/%.c
		$(COMPILE.c) $<


.PARALLEL: $(OBJS)
