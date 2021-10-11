#
#ident "@(#)Makefile	1.2	92/09/18 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= addbadsec

OBJECTS=	addbadsec.o  ix_altsctr.o
SRCS=		$(OBJECTS:.o=.c)

include ../Makefile.cmd

LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) $(PROG).mk

$(PROG):        $(OBJECTS)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
