#
#ident	"@(#)Makefile	1.8	95/02/25 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/ar/sparc/Makefile
#

PROG=		ar
XPG4PROG=	ar

ARFORMAT=	PORTAR

include		../../../Makefile.cmd

OBJECTS=	ar.o		sbfocus_enter.o
OBJS=		$(OBJECTS:%=objs/%)
XPG4OBJS=	$(OBJECTS:%=objs.xpg4/%)

SRCS=		$(OBJECTS:%.o=../common/%.c)

INCLIST=	-I../common -I../../include -I../../include/sparc
DEFLIST=	-D$(ARFORMAT) -DSPARC -DBROWSER -DELF
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS +=	-lelf -lintl
LINTFLAGS +=	$(LDLIBS)
$(XPG4) :=	CFLAGS += -DXPG4
CFLAGS +=	-v

CLEANFILES +=	$(LINTOUT)

objs/%.o objs.xpg4/%.o:	../common/%.c
		$(COMPILE.c) -o $@ $<

.KEEP_STATE:

all:		$(PROG) $(XPG4)

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(XPG4):	$(XPG4OBJS)
		$(LINK.c) $(XPG4OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(OBJS): objs

$(XPG4OBJS): objs.xpg4

objs objs.xpg4:
	-@mkdir -p $@

install:	all $(ROOTCCSBINPROG) $(ROOTXPG4PROG)

clean:
		$(RM) $(OBJS) $(XPG4OBJS) $(CLEANFILES)

lint:		$(LINTOUT)

$(LINTOUT):	$(SRCS)
		$(LINT.c) $(SRCS) > $(LINTOUT) 2>&1

include		../../../Makefile.targ
