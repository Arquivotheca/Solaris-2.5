#
#ident	"@(#)Makefile	1.3	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/dmesg/Makefile

PROG= dmesg

include ../Makefile.cmd

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
