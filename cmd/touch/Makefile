#
#ident	"@(#)Makefile	1.8	94/08/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= touch

include ../Makefile.cmd

LDLIBS += -lcmd -lgen -lintl
CFLAGS += -v
XGETFLAGS += -a -x touch.xcl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)
	-$(RM) $(ROOTBIN)/settime
	$(LN) -f $(ROOTPROG) $(ROOTBIN)/settime

clean:

lint:	lint_PROG

include ../Makefile.targ
