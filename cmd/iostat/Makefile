#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
#pragma	ident	"@(#)Makefile	1.4	92/08/20 SMI"
#

PROG= iostat
OBJS= iostat.o

include ../Makefile.cmd

LDLIBS += -lkstat
FILEMODE= 0555
OWNER= bin
GROUP= bin

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTPROG)

$(PROG):	$(OBJS)
	$(LINK.c) -o $(PROG) $(OBJS) $(LDLIBS)
	$(POST_PROCESS)
	
clean:
	-$(RM) $(OBJS)

lint:	lint_PROG

include ../Makefile.targ
