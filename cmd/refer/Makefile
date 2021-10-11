#
#ident	"@(#)Makefile	1.14	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# Makefile for cmd/refer
#

include		../Makefile.cmd

PROG1 =		refer addbib lookbib sortbib
PROG2 =		roffbib indxbib
PROG3 =		mkey inv hunt
PROG =		$(PROG1) $(PROG2)
$(PROG1):=	LDLIBS += -lintl
$(PROG3):=	LDLIBS += -lintl

SRCS =		addbib.c deliv2.c glue1.c glue2.c glue3.c glue4.c	\
		glue5.c hunt1.c hunt2.c hunt3.c hunt5.c hunt6.c		\
		hunt7.c hunt8.c hunt9.c inv1.c inv2.c inv3.c		\
		inv5.c inv6.c lookbib.c mkey1.c mkey2.c mkey3.c		\
		refer..c refer0.c refer1.c refer2.c refer3.c refer4.c	\
		refer5.c refer6.c refer7.c refer8.c shell.c sortbib.c	\
		tick.c types.c

OBJS =		$(SRCS:%.c=%.o)
#
# for messaging catalog
#
POFILES= $(OBJS:%.o=%.po)
POFILE= refer.po

addbib.po :=	XGETFLAGS += -a -x addbib.xcl


OTXTS =		tmac.bib

REFDIR =	$(ROOTLIB)/refer
TMACDIR =	$(ROOTSHLIB)/tmac
#Don't re-install dir already installed by Targetdirs
#ROOTDIRS =	$(REFDIR) $(TMACDIR)
ROOTDIRS =     $(REFDIR)

SUBDIRS =	papers

REFDIRPROG =	$(PROG3:%=$(REFDIR)/%)
IOTXTS =	$(OTXTS:%=$(TMACDIR)/%)

$(IOTXTS):=	FILEMODE = 0644

all:=		TARGET=	all
install:=	TARGET=	install
clean:=		TARGET=	clean
clobber:=	TARGET=	clobber
lint:=		TARGET=	lint

mkey:=		POBJS=  mkey1.o mkey2.o mkey3.o deliv2.o
inv:=		POBJS= inv1.o inv2.o inv3.o inv5.o inv6.o deliv2.o
hunt:=		POBJS= hunt1.o hunt2.o hunt3.o hunt5.o hunt6.o		\
			hunt7.o glue5.o refer3.o hunt9.o shell.o	\
			deliv2.o hunt8.o glue4.o tick.o
refer:=		POBJS= glue1.o refer1.o refer2.o refer4.o refer5.o	\
			refer6.o mkey3.o refer7.o refer8.o hunt2.o	\
			hunt3.o deliv2.o hunt5.o hunt6.o hunt8.o	\
			glue3.o hunt7.o hunt9.o glue2.o glue4.o		\
			glue5.o refer0.o shell.o
addbib:=	POBJS= addbib.o
lookbib:=	POBJS= lookbib.o	
sortbib:=	POBJS= sortbib.o

.KEEP_STATE :

all:		$(PROG) $(PROG3) $(OTXTS) $(SUBDIRS)

$(PROG1) $(PROG3): $$(POBJS)
		$(LINK.c) -o $@ $(POBJS) $(LDFLAGS) $(LDLIBS)
		$(POST_PROCESS)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@

types.po:
	touch types.po


install :	all $(ROOTDIRS) \
		$(ROOTPROG) $(REFDIRPROG) $(IOTXTS) $(SUBDIRS)
		
$(REFDIR)/% :	%
		$(INS.file)

$(TMACDIR)/% :	%
		$(INS.file)

clean :		$(SUBDIRS)
		$(RM) $(OBJS)

clobber :	$(SUBDIRS) local_clobber

local_clobber:
		$(RM) $(PROG) $(OBJS) $(CLOBBERFILES)

strip:

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

$(ROOTDIRS) :
		$(INS.dir)

FRC:

include ../Makefile.targ
