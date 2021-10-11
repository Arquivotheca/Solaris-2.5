#
#ident	"@(#)Makefile	1.4	93/06/11 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/size/sparc/Makefile
#

PROG=		size

include 	../../../Makefile.cmd

OBJS=		main.o process.o fcns.o

SRCS=		$(OBJS:%.o=../common/%.c)

INCLIST=	-I../common -I../../include -I../../include/sparc
DEFLIST=	-DSPARC -DELF
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS +=	-lelf
LINTFLAGS +=	$(LDLIBS)

CLEANFILES +=	$(LINTOUT)

%.o:		../common/%.c
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
