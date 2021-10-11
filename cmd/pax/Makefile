#
#ident	"@(#)Makefile	1.6	95/03/20 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# cmd/pax/Makefile
#
# note: you can modify the make as follows:
#
#       make COPTFLAG=-g ENVCPPFLAGS3="-DDEBUG"

include		../Makefile.cmd

PROG =		pax

OBJS =		append.o buffer.o cpio.o create.o extract.o \
		fileio.o hash.o isyesno.o link.o list.o main.o \
		mem.o namelist.o names.o pass.o pathname.o replace.o tar.o \
		ttyio.o warn.o

SRCS =		$(OBJS:%.o=%.c)

POFILES=	$(OBJS:%.o=%.po)

#CFLAGS +=	-v

LDLIBS +=	-lintl -lgen -lw

.PARALLEL:	$(OBJS)

.KEEP_STATE:

all :		$(PROG)

install :	all .WAIT $(ROOTPROG)

$(PROG) :	$(OBJS)
		$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
		$(POST_PROCESS)

$(POFILE):	$(POFILES)
		$(RM) $@
		cat $(POFILES) > $@

clean :
		$(RM) $(OBJS)

lint :		lint_SRCS

include		../Makefile.targ
