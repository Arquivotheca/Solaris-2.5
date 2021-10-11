#
#ident	"@(#)Makefile	1.11	95/02/21 SMI"
#
# Copyright (c) 1989, 1994 by Sun Microsystems, Inc.
#

PROG= renice

include ../Makefile.cmd

SRCS= setpriority.c renice.c
OBJS= setpriority.o renice.o

LDLIBS += -lintl
CFLAGS += -v

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS) $(PROG)

lint:	lint_SRCS

include ../Makefile.targ
