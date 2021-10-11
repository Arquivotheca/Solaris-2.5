#
# ident	"@(#)Makefile	1.8	95/02/10 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/expand/Makefile 
#

PROG=		expand  unexpand
SRCS=		$(PROG:=.c)

include		../Makefile.cmd

# workaround two meanings for expand.po
POFILES= expand_cmd.po unexpand.po
POFILE= expand.po

LDLIBS += -lintl -lw
CLEANFILES += expand_cmd.c

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint: lint_SRCS

$(POFILE): $(POFILES)
	$(RM) $@; cat $(POFILES) > $@

expand_cmd.c:
	$(RM) $@; cp expand.c $@

include ../Makefile.targ
