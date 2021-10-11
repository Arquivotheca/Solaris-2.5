#
# ident	"@(#)Makefile	1.11	94/08/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/lpstat/Makefile
#

include 	../../Makefile.lp

PROG=		lpstat

SRCS =		accept.c	\
		add_mounted.c	\
		charset.c	\
		class.c		\
		device.c	\
		done.c		\
		form.c		\
		lpstat.c	\
		output.c	\
		parse.c		\
		printer.c	\
		request.c	\
		send_message.c

OBJS=		$(SRCS:.c=.o)


CPPFLAGS =	-I$(LPINC) $(CPPFLAGS.master)

LPLIBS =	$(LIBACC)	\
		$(LIBCLS)	\
		$(LIBMSG)	\
		$(LIBPRT)	\
		$(LIBFRM)	\
		$(LIBOAM)	\
		$(LIBLP)

SYSLIBS=	-lcurses

LDLIBS +=	$(LPLIBS) $(SYSLIBS) -lintl -lw

.KEEP_STATE:
# for messaging catalog files
#
POFILE= lpstat.po
XGETFLAGS += -a -x $(LPROOT)/lp.xcl

all:		$(PROG)

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM)	$@
	$(COMPILE.cpp)   $(SRCS) > $(POFILE).i
	$(XGETTEXT)     $(XGETFLAGS)    $(POFILE).i
	sed "/^domain/d"        messages.po     > $@
	$(RM) messages.po $(POFILE).i


install:	all $(ROOTPROG)

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint:		lint_SRCS

include		../../../Makefile.targ
