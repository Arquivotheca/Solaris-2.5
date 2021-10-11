#
#ident	"@(#)Makefile	1.1	94/11/18 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#

PROG= locale

OBJS= locale.o
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
