#
#pragma ident	"@(#)Makefile	1.17	94/07/18 SMI"
#
# Copyright (c) 1989-1993 by Sun Microsystems, Inc.
#

PROG= cpio

OBJS= cpio.o cpiostat.o
SRCS= $(OBJS:%.o=%.c)

include ../Makefile.cmd

LDLIBS += -lgen -lintl -lmapmalloc -lsec
XGETFLAGS += -a -x cpio.xcl

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
