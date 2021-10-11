#
# ident	"@(#)Makefile	1.5	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/lpdata/Makefile
#

include 	../../Makefile.lp

PROG=		lpdata

SRCS= 		lpdata.c

OBJS=		$(SRCS:.c=.o)


CPPFLAGS =	-I$(LPINC) $(CPPFLAGS.master)

LPLIBS=		$(LIBMSG) $(LIBFRM) $(LIBREQ) $(LIBPRT) \
		$(LIBCLS) $(LIBACC) $(LIBFLT) $(LIBUSR) \
		$(LIBOAM) $(LIBLP)

SYSLIBS=	-lgen -lintl

LDLIBS +=	$(LPLIBS) $(SYSLIBS)

.KEEP_STATE:

all:		$(PROG)

install:	all $(ROOTLIBLPPROG)

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint:		lint_PROG

include		../../../Makefile.targ
