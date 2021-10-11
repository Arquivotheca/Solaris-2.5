#
# ident	"@(#)Makefile	1.6	92/12/23 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/truss/Makefile
#

PROG=	truss

OBJS=	main.o listopts.o ipc.o actions.o expound.o codes.o print.o \
	pcontrol.o ramdata.o systable.o procset.o xstat.o

SRCS=	$(OBJS:%.o=%.c)

include	../Makefile.cmd

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include	../Makefile.targ
