#
#ident	"@(#)Makefile	1.5	94/11/13 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/strip/sparc/Makefile
#

PROG=		strip

ARFORMAT=	PORTAR

include 	../../../Makefile.cmd

OBJS=		main.o

SRCS=		$(OBJS:%.o=../common/%.c)

INCLIST=	-I../../include -I../../include/sparc
DEFLIST=	-D$(ARFORMAT) -DELF
CPPFLAGS=	$(INCLIST) $(DEFLIST) $(CPPFLAGS.master)
LDLIBS +=	-lelf -lintl
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
