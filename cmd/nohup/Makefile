#
#ident	"@(#)Makefile	1.7	94/12/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

XPG4PROG= nohup
PROG= nohup

include ../Makefile.cmd
LDLIBS += -lintl
CFLAGS += -v
$(XPG4) := CFLAGS += -DXPG4

.KEEP_STATE:

all: $(PROG) $(XPG4)

install: all $(ROOTPROG) $(ROOTXPG4PROG)

clean:

# do not lint nohup.xpg4
lint:	lint_PROG

include ../Makefile.targ
