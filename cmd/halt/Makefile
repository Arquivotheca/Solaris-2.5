#
#ident	"@(#)Makefile 1.12	94/07/27 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= halt
LINKPROG= poweroff

include ../Makefile.cmd

FILEMODE=	755

USRSYMLINK= $(ROOTUSRSBIN)/$(LINKPROG)

ROOTSYMLINKS= $(ROOTETCPROG)


.KEEP_STATE:

all: $(PROG) 

LDLIBS += -lbsm -lsocket -lnsl -lintl -lgen

install: all $(ROOTUSRSBINPROG) $(USRSYMLINK) $(ROOTSYMLINKS)

$(USRSYMLINK):
	$(RM) $@
	$(SYMLINK) ./$(PROG) $@

$(ROOTSYMLINKS):
	$(RM) $@
	$(SYMLINK) ../usr/sbin/$(PROG) $@

clean:

lint:	lint_PROG

include ../Makefile.targ
