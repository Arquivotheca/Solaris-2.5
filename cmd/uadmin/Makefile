#
#ident	"@(#)Makefile	1.13	94/04/28 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

# build two versions, dynamic and static
PROG= uadmin
STATPROG= uadmin

# install rule for static version
# deliberately ahead of a similar rule in ../Makefile.cmd
#
ROOT= /proto
$(ROOT)/sbin/%: %.static
	$(INS.rename)

include ../Makefile.cmd

OWNER = root
GROUP = sys
CLOBBERFILES= $(STATIC)

.KEEP_STATE:

all: $(PROG) $(STATIC) 

install: all $(ROOTSBINPROG) $(ROOTUSRSBINPROG)
	-$(RM) $(ROOTETCPROG)
	-$(SYMLINK) ../sbin/$(PROG) $(ROOTETCPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
