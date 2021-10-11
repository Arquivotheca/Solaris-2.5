#
#ident	"@(#)Makefile	1.11	95/02/06 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All Rights Reserved

PROG= expr
OBJS= expr.o compile.o
SRCS= $(OBJS:%.o=%.c)

include ../Makefile.cmd

CFLAGS += -v
XGETFLAGS += -a -x expr.xcl

LDLIBS += -lgen -lintl -lw

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
