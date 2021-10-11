#
#ident	"@(#)Makefile	1.2	94/12/12 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/cksum/Makefile

PROG= cksum

OBJS= cksum.o

CLOBBERFILES= $(PROG) $(PROG2)

include ../Makefile.cmd

CPPFLAGS= -I. $(CPPFLAGS.master)
LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

lint: lint_SRCS

clean:
	$(RM) $(OBJS)

include ../Makefile.targ
