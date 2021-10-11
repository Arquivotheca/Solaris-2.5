#
#ident	"@(#)Makefile	1.8	94/10/17 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# cmd/chmod/Makefile
#

PROG= chmod
OBJS= chmod.o common.o
SRCS= $(OBJS:%.o=%.c)

POFILES= $(OBJS:%.o=%.po)

include ../Makefile.cmd

CFLAGS += -v
LDLIBS += -lintl

POFILE= chmod_cmd.po
XGETFLAGS= -a -x chmod.xcl

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

install: all $(ROOTPROG)

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../Makefile.targ
