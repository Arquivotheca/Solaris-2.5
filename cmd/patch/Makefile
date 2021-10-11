#
#ident	"@(#)Makefile	1.1	95/03/01 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
#

PROG=	patch

SRCS=	patch.c pch.c util.c file.c

OBJS=	$(SRCS:%.c=%.o)

include ../Makefile.cmd

LDLIBS += -lgen -lw -lintl
CFLAGS += -v

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

clean:

lint:	lint_SRCS

patch:	$(SRCS)
	$(LINK.c) $(SRCS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

include		../Makefile.targ

