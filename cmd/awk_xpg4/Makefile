#
#ident  "@(#)Makefile 1.1	95/03/09 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
#
# cmd/awk_xpg4/Makefile
#

# NOTE: this is nawk in usr/src/cmd/awk_xpg4 to reside as /usr/xpg4/bin/awk

PROG= awk
XPG4PROG= awk

OBJ1= awk0.o awk1.o awk2.o awk3.o awk4.o
OBJ2= awk.o
OBJS= $(OBJ2) $(OBJ1)
SRCS= $(OBJ1:%.o=%.c)

include ../Makefile.cmd

CFLAGS += -v
YFLAGS += -d
LDLIBS += -lm -lw -lintl
CLEANFILES= awk.c y.tab.h

# for messaging catalog

POFILE= awk_xpg4.po
POFILES= $(OBJS:%.o=%.po)

.KEEP_STATE:

all: $(XPG4)

$(XPG4): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@
#

install: all $(ROOTXPG4PROG)

clean:
	$(RM) $(OBJS) $(CLEANFILES)

lint: lint_SRCS

awk.c + y.tab.h: awk.y

awk.o: awk.c y.tab.h

include ../Makefile.targ
