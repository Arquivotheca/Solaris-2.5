#
#ident	"@(#)Makefile 1.7 93/10/26 SMI;"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# cmd/auditreduce/Makefile

PROG= auditreduce
OBJS= main.o option.o proc.o time.o token.o regex2.o

include ../Makefile.cmd

TEXT_DOMAIN=SUNW_BSM_AUDITREDUCE
POS=main.po option.po proc.po time.po token.po
include ../Makefile.cmd.bsm

SRCS= $(OBJS:.o=.c)

LDLIBS += -lnsl -lgen -lbsm -lintl

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $(PROG) $(OBJS) $(LDFLAGS) $(LDLIBS)

lint: lint_SRCS

clean:
	rm -f $(OBJS)
	rm -f $(PROG)

tags: $(SRCS)
	ctags $(SRCS)

include ../Makefile.targ
