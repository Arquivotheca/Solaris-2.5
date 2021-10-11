#
#ident	"@(#)Makefile	1.8	93/12/12 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/dis/sparc/Makefile
#

PROG=		dis

include 	../../../Makefile.cmd

COMOBJS=	debug.o extn.o lists.o main.o utls.o
MACHOBJS=	bits.o disasm.o sas_tables.o tables.o
DEMOBJS=	dem.o cafe_dem.o

OBJS=		$(COMOBJS) $(MACHOBJS) $(DEMOBJS)
SRCS=		$(COMOBJS:%.o=../common/%.c) $(MACHOBJS:.o=.c) \
		$(DEMOBJS:%.o=../../c++/demangler/common/%.c)

INCLIST=	-I. -I../common -I../../include -I../../include/sparc
DEFLIST=	-DSUN4 -DM32 -DFBO -DSPARC -DELF
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS +=	-lelf
LINTFLAGS +=	$(LDLIBS)
CLEANFILES +=	$(LINTOUT)

$(DEMOBJS):=	INCLIST = -I../../c++/demangler/common
$(DEMOBJS):=	DEFLIST = -DELF_OBJ

%.o:		../common/%.c
		$(COMPILE.c) $<

%.o:		../../c++/demangler/common/%.y
		$(YACC.y) $<
		$(COMPILE.c) -o $@ y.tab.c
		$(RM) y.tab.c

%.o:		../../c++/demangler/common/%.c
		$(COMPILE.c) $<

.KEEP_STATE:

all:		$(PROG)

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

install:	all $(ROOTCCSBINPROG)

clean:
		$(RM) $(OBJS) $(CLEANFILES)

lint:		$(LINTOUT)

$(LINTOUT):	$(SRCS)
		$(LINT.c) $(SRCS) > $(LINTOUT) 2>&1

include		../../../Makefile.targ
