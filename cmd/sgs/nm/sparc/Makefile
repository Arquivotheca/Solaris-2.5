#
#ident	"@(#)Makefile	1.10	95/03/20 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All Rights Reserved
#
# cmd/sgs/nm/sparc/Makefile
#

PROG=		nm
XPG4PROG=	nm

ARFORMAT=	PORTAR

include 	../../../Makefile.cmd

COMOBJS=	nm.o
DEMOBJS=	dem.o cafe_dem.o

OBJS=		$(COMOBJS) $(DEMOBJS)
XPG4OBJS=	objs.xpg4/nm.o

SRCS=		$(COMOBJS:%.o=../common/%.c) \
		$(DEMOBJS:%.o=../../c++/demangler/common/%.c)

INCLIST=	-I../../include -I../../include/sparc
DEFLIST=	-DTARGET=SPARC -DSPARC=1 -D$(ARFORMAT) -DELF
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS +=	-lelf -lintl
LINTFLAGS +=	$(LDLIBS)
CLEANFILES +=	$(LINTOUT)
objs.xpg4/%.o := CPPFLAGS += -DXPG4

$(DEMOBJS):=	INCLIST = -I../../c++/demangler/common
$(DEMOBJS):=	DEFLIST = -DELF_OBJ

objs.xpg4/%.o:	../common/%.c
		$(COMPILE.c) -o $@ $<

%.o:		../common/%.c
		$(COMPILE.c) $<

%.o:		../../c++/demangler/common/%.y
		$(YACC.y) $<
		$(COMPILE.c) -o $@ y.tab.c
		$(RM) y.tab.c

%.o:		../../c++/demangler/common/%.c
		$(COMPILE.c) $<

.KEEP_STATE:

all:		$(PROG) $(XPG4)

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(XPG4):	$(XPG4OBJS) $(DEMOBJS)
		$(LINK.c) $(XPG4OBJS) $(DEMOBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(XPG4OBJS):	objs.xpg4

objs.xpg4:
	-@mkdir -p $@

install:	all $(ROOTCCSBINPROG) $(ROOTXPG4PROG)

clean:
		$(RM) $(OBJS) $(XPG4OBJS) $(CLEANFILES)

lint:		$(LINTOUT)

$(LINTOUT):	$(SRCS)
		$(LINT.c) $(SRCS) > $(LINTOUT) 2>&1

include		../../../Makefile.targ
