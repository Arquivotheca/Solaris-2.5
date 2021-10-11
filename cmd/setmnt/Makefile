#
#ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= setmnt 

include ../Makefile.cmd

.KEEP_STATE:

all: $(PROG) 

install: all  $(ROOTUSRSBINPROG)
	-$(RM) $(ROOTETCPROG)
	-$(SYMLINK) ../usr/sbin/$(PROG) $(ROOTETCPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
