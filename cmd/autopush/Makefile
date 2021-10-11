#
#ident	"@(#)Makefile	1.11	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

# build two versions, dynamic and static
PROG= autopush 
STATPROG= autopush 

# install rule for static version
# deliberately ahead of a similar rule in ../Makefile.cmd
#
ROOT= /proto
$(ROOT)/sbin/%: %.static
	$(INS.rename)

include ../Makefile.cmd

LDLIBS += -lintl
CLOBBERFILES += $(STATIC)

.KEEP_STATE:

all: $(PROG) $(STATIC) 

install: all $(ROOTSBINPROG) $(ROOTUSRSBINPROG)
	$(RM) $(ROOTETCPROG)
	-$(SYMLINK) ../sbin/$(PROG) $(ROOTETCPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
