#
# ident	"@(#)Makefile	1.16	93/09/17 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/lp/cmd/lpsched/lpsched/Makefile
#

PROG=		lpsched

include		../../../Makefile.lp

PURIFYOPTS      = -logfile=/tmp/errs.%p
PURIFY          = purify $(PURIFYOPTS)

#CHECK_CHILDREN	= -DCHECK_CHILDREN
LOST_LOCK	= -DLOST_LOCK
#MALLOC_3X	= -DMALLOC_3X -DDEF_MXFAST=32 -DDEF_NLBLKS=32 -DDEF_GRAIN=4
#LDMALLOC	= -lmalloc
#TRACE_MALLOC	= -DTRACE_MALLOC
#LDDEBUG	= malloc/mymalloc.c

# Doing -DDEBUG allows some nice log files to be generated
# with the -d option.

MYDEBUG=	$(TRACE_MALLOC) $(MALLOC_INC)

CPPFLAGS =	-I. -I$(LPINC) $(MYDEBUG)	\
		$(CHECK_CHILDREN)		\
		$(LOST_LOCK)			\
		$(MALLOC_3X)			\
		$(CPPFLAGS.master)

HDRS=				\
		nodes.h		\
		dispatch.h	\
		validate.h	\
		lpsched.h

SRCS=				\
		alerts.c	\
		cancel.c	\
		checkchild.c	\
		daisyforms.c	\
		disena.c	\
		disp1.c		\
		disp2.c		\
		disp3.c		\
		disp4.c		\
		disp5.c		\
		disptab.c	\
		dowait.c	\
		exec.c		\
		faults.c	\
		files.c		\
		flt.c		\
		fncs.c		\
		getkey.c	\
		getpwent.c	\
		getstatus.c	\
		init.c		\
		log.c		\
		lpfsck.c	\
		lpsched.c	\
		msgs.c		\
		notify.c	\
		pickfilter.c	\
		ports.c		\
		putjob.c	\
		rexec.c		\
		requeue.c	\
		rstatus.c	\
		schedule.c	\
		status.c	\
		terminate.c	\
		validate.c

OBJS=		$(SRCS:.c=.o)


LPLIBS =			\
		$(LIBMSG)	\
		$(LIBFRM)	\
		$(LIBREQ)	\
		$(LIBPRT)	\
		$(LIBCLS)	\
		$(LIBACC)	\
		$(LIBFLT)	\
		$(LIBUSR)	\
		$(LIBOAM)	\
		$(LIBLP)	\
		$(LIBSEC)	\
		$(LIBSYS)

SYSLIBS=	-lcurses -lgen -lcurses -lnsl

LDLIBS +=	$(LPLIBS) $(SYSLIBS) -lintl -lw

ROOTSYMLNKPROG =	$(ROOTLIBPROG)
SYMLNKDEST =		lp/$(PROG)

# for message catalog
#
POFILE= lpsched.po
XGETFLAGS += -a -x $(LPROOT)/lp.xcl


.KEEP_STATE:

all:		$(PROG)

$(PROG):	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(PROG).pure:	$(OBJS)
		$(PURIFY) $(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM)	$(POFILE)
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT)     $(XGETFLAGS)    $(POFILE).i
	sed "/^domain/d"        messages.po > $(POFILE)
	$(RM) $(POFILE).i messages.po

install:	all $(ROOTLIBLPPROG) $(ROOTSYMLNKPROG)

$(ROOTSYMLNKPROG) :
		$(RM) $@; $(SYMLINK) $(SYMLNKDEST) $@

clean:
		$(RM) $(OBJS)

strip:
		$(STRIP) $(PROG)

lint:		lint_SRCS

include		../../../../Makefile.targ
