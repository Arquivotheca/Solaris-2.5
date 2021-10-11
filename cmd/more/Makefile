#
#ident	"@(#)Makefile	1.7	95/01/11 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= more
DATA= more.help

include ../Makefile.cmd

LDLIBS += -lcmd -lcurses -lgen -lintl

ROOTLIBDATA= $(DATA:%=$(ROOTLIB)/%)

$(ROOTLIBDATA) :=	FILEMODE =	0644

CFLAGS += -v

.KEEP_STATE:

all: $(PROG) $(DATA) 

install: all $(ROOTPROG) $(ROOTLIBDATA)
	$(RM) $(ROOTBIN)/page
	$(LN) $(ROOTPROG) $(ROOTBIN)/page

clean:

lint:	lint_PROG

include ../Makefile.targ
