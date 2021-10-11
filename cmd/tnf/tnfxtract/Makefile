#
# ident	"@(#)Makefile 1.3 95/02/27 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/tnf/tnfxtract/Makefile
#

PROG=		tnfxtract

OBJS.c=		tnfxtract.o

OBJS=		$(OBJS.c)

SRCS= $(OBJS.c:%.o=%.c)

include	../../Makefile.cmd

LFLAGS=		-v
CPPFLAGS +=	-I../../../lib/libtnfprobe
LDLIBS +=	-ltnfprobe -lgen -lintl -lkvm -lelf

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include	../../Makefile.targ
