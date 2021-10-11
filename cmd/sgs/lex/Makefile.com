#
#ident	"@(#)Makefile.com	1.3	94/07/26 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/lex/Makefile.com
#

MACHOBJS=	main.o sub1.o sub2.o sub3.o header.o parser.o
WHATOBJS=	whatdir.o
POBJS=		$(MACHOBJS) $(WHATOBJS)

LIBOBJS=	allprint.o libmain.o reject.o yyless.o yywrap.o
LIBOBJS_W=	allprint_w.o reject_w.o yyless_w.o
LIBOBJS_E=	reject_e.o yyless_e.o
OBJS=		$(LIBOBJS) $(LIBOBJS_W) $(LIBOBJS_E)

FORMS=		nceucform ncform nrform

SRCS=		$(MACHOBJS:%.o=../common/%.c) \
		$(WHATOBJS:%.o=../../whatdir/common/%.c) \
		$(LIBOBJS:%.o=../common/%.c)

LINTSRCS=	../common/llib-l$(LIBNAME)

INCLIST=	$(INCLIST_$(MACH)) -I../../include -I../../include/$(MACH)
DEFLIST=	-DELF
$(LIBOBJS_W):=	DEFLIST = -DEUC -DJLSLEX  -DWOPTION -D$*=$*_w
$(LIBOBJS_E):=	DEFLIST = -DEUC -DJLSLEX  -DEOPTION -D$*=$*_e
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS=		$(LDLIBS.cmd) -lw -lgen
BUILD.AR=	$(AR) $(ARFLAGS) $@ `$(LORDER) $(OBJS) | $(TSORT)`
LINTFLAGS=	-ax $(LDLIBS)
LINTPOUT=	lintp.out

$(LINTLIB):=	LINTFLAGS = -nvxc $(LDLIBS)
$(ROOTCCSBINPROG):= FILEMODE = 0555

ROOTLIBDIR=	$(ROOT)/usr/ccs/lib
ROOTFORMS=	$(FORMS:%=$(ROOTCCSBIN)/%)

CLEANFILES +=	parser.c $(LINTPOUT) $(LINTOUT)
CLOBBERFILES +=	$(LIBS)

LIBS +=		$(LINTLIB)

%_w.o %_e.o:	../common/%.c
		$(COMPILE.c) -o $@ $<
		$(POST_PROCESS_O)

%.o:		../../whatdir/common/%.c
		$(COMPILE.c) $<

%.o:		../common/%.c
		$(COMPILE.c) $<

$(ROOTLIBDIR)/%: %
	$(INS.file)

$(ROOTCCSBIN)/%: ../common/%
	$(INS.file)
