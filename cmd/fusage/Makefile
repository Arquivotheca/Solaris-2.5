#
#ident	"@(#)Makefile	1.4	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fusage/Makefile

PROG= fusage

include ../Makefile.cmd

#LDFLAGS = -L$(ROOT)/usr/ccs/lib -s
LDLIBS += -lkvm -lelf

GROUP= sys
FILEMODE= 02555

ROOTSYMLINK= $(ROOTPROG)

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTUSRSBINPROG) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	$(RM) $@; $(SYMLINK) ../sbin/$(PROG) $@

clean:

lint:	lint_PROG

include ../Makefile.targ
