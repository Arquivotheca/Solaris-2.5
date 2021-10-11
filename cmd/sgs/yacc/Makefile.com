#
#ident	"@(#)Makefile.com	1.2	94/08/26 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/yacc/Makefile.com
#

COMOBJS=	y1.o y2.o y3.o y4.o
WHATOBJS=	whatdir.o
OBJS=		libmai.o libzer.o
POBJS=		$(COMOBJS) $(WHATOBJS)
YACCPAR=	yaccpar

SRCS=		$(COMOBJS:%.o=../common/%.c) \
		$(WHATOBJS:%.o=../../whatdir/common/%.c) \
		$(OBJS:%.o=../common/%.c)
LINTSRCS=	../common/llib-l$(LIBNAME)

INCLIST=	-I../../include -I../../include/$(MACH)
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS=		$(LDLIBS.cmd) -lw -lintl
BUILD.AR=	$(AR) $(ARFLAGS) $@ `$(LORDER) $(OBJS) | $(TSORT)`
LINTFLAGS=	-ax $(LDLIBS)
LINTPOUT=	lintp.out
$(LINTLIB):=	LINTFLAGS = -nvxc $(LDLIBS)
$(ROOTCCSBINPROG):= FILEMODE = 0555

ROOTLIBDIR=	$(ROOT)/usr/ccs/lib
ROOTYACCPAR=	$(YACCPAR:%=$(ROOTCCSBIN)/%)

CLEANFILES +=	$(LINTPOUT) $(LINTOUT)
CLOBBERFILES +=	$(LIBS)

LIBS +=		$(LINTLIB)

%.o:		../common/%.c
		$(COMPILE.c) $<
		$(POST_PROCESS_O)

%.o:		../../whatdir/common/%.c
		$(COMPILE.c) $<

$(ROOTLIBDIR)/%: %
	$(INS.file)

$(ROOTCCSBIN)/%: ../common/%
	$(INS.file)
