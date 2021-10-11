#
#ident	"@(#)Makefile	1.7	94/11/07 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= gencat

OBJS= gencat.o
SRCS= $(OBJS:%.o=%.c)

include ../Makefile.cmd
LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include ../Makefile.targ
