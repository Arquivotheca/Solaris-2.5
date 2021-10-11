#
#ident	"@(#)Makefile	1.6	94/09/01 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= compress

include ../Makefile.cmd

.KEEP_STATE:

CFLAGS += -v
LDLIBS +=       -lintl

all: $(PROG) 

install: all $(ROOTPROG)
	$(RM) $(ROOTBIN)/uncompress $(ROOTBIN)/zcat
	$(LN) $(ROOTPROG) $(ROOTBIN)/uncompress
	$(LN) $(ROOTPROG) $(ROOTBIN)/zcat

clean:

lint:	lint_PROG

include ../Makefile.targ
