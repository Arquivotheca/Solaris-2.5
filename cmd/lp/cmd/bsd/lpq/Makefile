#
# ident	"@(#)Makefile	1.14	94/03/29 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/bsd/lpq/Makefile
#

include 	../../../Makefile.lp

PROG=		lpq

SRCS= 		lpq.c

OBJS=		$(SRCS:.c=.o)


CPPFLAGS =	-I$(LPINC) $(CPPFLAGS.master)	-DTERMCAP
LDLIBS +=	$(LIBBSD) $(LIBREQ) $(LIBMSG) $(LIBOAM) \
		$(LIBPRT) $(LIBSYS) $(LIBCLS) $(LIBLP) \
		-lgen -lcurses -lintl -lw

.KEEP_STATE:

# for messaging
#
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl

catalog:        $(POFILE)

all:		$(PROG)

install:	all $(ROOTUSRUCBPROG)

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint:		lint_PROG

include		../../../../Makefile.targ
