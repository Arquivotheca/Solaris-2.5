#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#pragma	ident	"@(#)Makefile	1.1	93/04/01 SMI"
#

PROG= mpstat
OBJS= mpstat.o

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
