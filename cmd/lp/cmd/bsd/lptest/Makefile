#
# ident	"@(#)Makefile	1.4	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/bsd/lptest/Makefile
#

include 	../../../Makefile.lp

PROG=		lptest

SRCS= 		lptest.c

OBJS=		$(SRCS:.c=.o)


CPPFLAGS =	-I$(LPINC) $(CPPFLAGS.master)
LDLIBS +=	$(LIBBSD) $(LIBREQ) $(LIBMSG) $(LIBOAM) \
		$(LIBPRT) $(LIBSEC) $(LIBLP) -lgen -lcurses

.KEEP_STATE:

all:		$(PROG)

install:	all $(ROOTUSRUCBPROG)

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint :		lint_PROG

include		../../../../Makefile.targ
