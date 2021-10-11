#
#ident	"@(#)Makefile	1.6	94/08/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= link 

include ../Makefile.cmd

OWNER= root
FILEMODE= 0555

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTUSRSBINPROG)
	-$(RM) $(ROOTETCPROG)
	-$(SYMLINK) ../usr/sbin/$(PROG) $(ROOTETCPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
