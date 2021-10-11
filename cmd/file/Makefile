#
#ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= file
MAGIC= magic

include ../Makefile.cmd

LDLIBS += -lcmd -lelf -lw -lintl

ROOTETCMAGIC= $(MAGIC:%=$(ROOTETC)/%)

$(ROOTETCMAGIC) :=	FILEMODE =	$(LIBFILEMODE)

.KEEP_STATE:

all: $(PROG) $(MAGIC) 

install: all $(ROOTPROG) $(ROOTETCMAGIC)

clean:

lint: lint_PROG

include ../Makefile.targ
