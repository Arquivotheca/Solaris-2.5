#
#ident	"@(#)Makefile	1.11	95/01/10 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/tnf/prex/Makefile
#

PROG=		prex

OBJS.c=		source.o	\
		main.o		\
		util.o		\
		expr.o		\
		spec.o		\
		set.o		\
		queue.o		\
		cmd.o		\
		new.o		\
		list.o		\
		fcn.o		\
		help.o		\
		comb.o		\
		status.o	\
		targmem.o	\
		shmem.o		\
		child.o		\
		proc.o		\
		elf.o		\
		rtld.o		\
		traverse.o	\
		sym.o		\
		link.o		\
		lmap.o		\
		prbk.o

OBJS.yl=	prexgram.o	\
		prexlex.o

OBJS.s=		$(MACH)_assm.o

OBJS=		$(OBJS.c) $(OBJS.s) $(OBJS.yl)

# We omit $(OBJS.s:%.o=%.s) in the next line, because lint no like
SRCS= $(OBJS.c:%.o=%.c) $(OBJS.yl:%.o=%.c)

include	../../Makefile.cmd

POFILE= prex.po
POFILES= $(OBJS.c:%.o=%.po)

#YFLAGS=	-d -t -v
YFLAGS=		-d
LFLAGS=		-v
CPPFLAGS +=	-I../../../lib/libtnfprobe
LDLIBS +=	-lelf -lgen -lintl
LINTFLAGS +=	-lelf

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTBIN) $(ROOTPROG)

$(ROOTBIN):
	$(INS.dir)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@

clean:
	$(RM) $(OBJS) y.tab.h prexlex.c prexgram.c

lint: $(SRCS) lint_SRCS

include	../../Makefile.targ
