#
# @(#)Makefile 1.10 93/04/27 SMI
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/praudit/Makefile

PROG = praudit

include ../Makefile.cmd
include ../Makefile.cmd.bsm
TEXT_DOMAIN=SUNW_BSM_PRAUDIT
PO=$(TEXT_DOMAIN).po

LDLIBS += -lbsm -lsocket -lnsl -lintl

OBJS = praudit.o
SRCS = praudit.c

.KEEP_STATE:

all: $(PROG)

praudit_c.po:
	xgettext -c TRANSLATION_NOTE -d $(TEXT_DOMAIN) praudit.c
	mv $(PO) praudit_c.po

structures.po:
	sccs get -s structures.po

$(PO):	praudit_c.po structures.po
	cat praudit_c.po structures.po > $@

clean:
	rm -f $(OBJS) $(POS)

include ../Makefile.targ
