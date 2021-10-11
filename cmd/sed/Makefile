#
#ident	"@(#)Makefile	1.8	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= sed

OBJS= sed0.o sed1.o
SRCS= $(OBJS:%.o=%.c)

include ../Makefile.cmd

LDLIBS += -lgen -lw -lintl

# for messaging catalog
POFILE= sed.po
POFILES= $(OBJS:%.o=%.po)
XGETFLAGS += -a -x sed.xcl


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

lint: lint_SRCS

include ../Makefile.targ
