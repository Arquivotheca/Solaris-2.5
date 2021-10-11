#
# ident	"@(#)Makefile	1.12	94/08/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/lpadmin/Makefile
#

include		../../Makefile.lp

CPPFLAGS =	-I. -I$(LPINC) $(CPPFLAGS.master)

SRCS=		chkopts.c	\
		default.c	\
		do_align.c	\
		do_fault.c	\
		do_mount.c	\
		do_printer.c	\
		do_pwheel.c	\
		done.c		\
		fromclass.c	\
		ismodel.c	\
		lpadmin.c	\
		options.c	\
		output.c	\
		rmdest.c	\
		send_message.c	\
		signals.c	\
		startup.c	\
		usage.c

OBJS=		$(SRCS:.c=.o)

LPLIBS=		$(LIBACC)	\
		$(LIBCLS)	\
		$(LIBMSG)	\
		$(LIBPRT)	\
		$(LIBFRM)	\
		$(LIBREQ)	\
		$(LIBOAM)	\
		$(LIBSYS)	\
		$(LIBLP)

SYSLIBS=	-lcurses

LDLIBS +=	$(LPLIBS) $(SYSLIBS) -lintl -lw

PROG=		lpadmin


ROOTSYMLINK=	$(ROOTLIBPROG)

# for messaging catalog
#
POFILE= lpadmin.po
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl


.KEEP_STATE:

all:		$(PROG)

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

catalog:        $(POFILE)

$(POFILE):      $(SRCS)
		$(RM)	$@
		$(COMPILE.cpp)   $(SRCS) > $(POFILE).i
		$(XGETTEXT)     $(XGETFLAGS)    $(POFILE).i
		sed "/^domain/d"        messages.po > $@
		$(RM) messages.po $(POFILE).i

install:	all $(ROOTUSRSBINPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
		$(RM) $@; $(SYMLINK) ../sbin/$(PROG) $@

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint:		lint_SRCS

include		../../../Makefile.targ
