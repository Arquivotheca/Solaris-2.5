#
#ident	"@(#)Makefile	1.5	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG=		in.routed
OBJS=		af.o if.o inet.o input.o main.o output.o \
		startup.o tables.o timer.o trace.o
SRCS=		$(OBJS:%.o=%.c)
TOOLS=		tools/query.c tools/trace.c

include		../../../Makefile.cmd

CPPFLAGS +=	-DSYSV -DSTRNET -DBSD_COMP
LDLIBS +=	-lsocket -lnsl

.KEEP_STATE:

all: $(PROG) $(TOOLS)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG)

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include		../../../Makefile.targ
