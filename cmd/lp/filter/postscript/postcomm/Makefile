#
# ident	"@(#)Makefile	1.3	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/filter/postscript/postcomm/Makefile
#

include		../../../Makefile.lp

PROG=		postcomm

SRCS=		postcomm.c

OBJS=		$(SRCS:.c=.o)


COMMONDIR =	../common
CPPFLAGS =	-I. -I$(COMMONDIR) $(CPPFLAGS.master)

.KEEP_STATE:

all:		$(PROG)

install:	all $(ROOTLIBLPPOSTPROG)

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint:		lint_PROG

include		../../../../Makefile.targ
