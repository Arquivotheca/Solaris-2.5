#
# ident	"@(#)Makefile	1.8	93/08/12 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/bsd/lpr/Makefile
#

include 	../../../Makefile.lp

PROG=		lpr

SRCS= 		lpr.c

OBJS=		$(SRCS:.c=.o)


CPPFLAGS =	-I$(LPINC) $(CPPFLAGS.master)
LDLIBS +=	$(LIBBSD) $(LIBREQ) $(LIBMSG) $(LIBOAM) \
		$(LIBPRT) $(LIBLP) -lgen -lintl -lw

# for messaging catalog
#
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl


.KEEP_STATE:

all:		$(PROG)

catalog:        $(POFILE)


install:	all $(ROOTUSRUCBPROG)

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint:		lint_PROG

include		../../../../Makefile.targ
