#
#ident	"@(#)Makefile 1.4	95/03/07 SMI"
#
# Copyright (c), 1995 by Sun Microsystems, Inc.
# All rights reserved.
#

PROG= more
XPG4PROG= more
OBJS= ch.o command.o decode.o help.o input.o line.o linenum.o main.o \
      option.o os.o output.o position.o prim.o screen.o signal.o tags.o \
      ttyin.o
SRCS= $(OBJS:%.o=%.c)
DATA= more.help

POFILES= $(OBJS:%.o=%.po)

include ../Makefile.cmd

CFLAGS += -v
LDLIBS += -lcmd -lcurses -lgen -lintl -lw

POFILE=more_xpg4.po
XGETFLAGS= -a -x more.xcl

.KEEP_STATE:

.PARALLEL: $(OBJS)

all: $(XPG4)

$(XPG4): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

install: all $(ROOTXPG4PROG)

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../Makefile.targ
