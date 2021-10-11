#
#ident	"@(#)Makefile	1.8	93/11/22 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= reboot

include ../Makefile.cmd

FILEMODE=	755

ROOTSYMLINK= $(ROOTETCPROG)

.KEEP_STATE:

all: $(PROG) 

LDLIBS += -lbsm -lsocket -lnsl -lintl

install: all $(ROOTUSRSBINPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	$(RM) $@; $(SYMLINK) ../usr/sbin/$(PROG) $@

clean:

lint:	lint_PROG

include ../Makefile.targ
