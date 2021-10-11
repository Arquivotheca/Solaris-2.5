#
# @(#)Makefile 1.7 93/04/19 SMI; SunOS 5.X
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/dminfo/Makefile

PROG= dminfo

OBJS=	dminfo.o
SRCS=	$(OBJS:%.o=%.c)

include ../Makefile.cmd

TEXT_DOMAIN=SUNW_BSM_DMINFO
POS=dminfo.po
include ../Makefile.cmd.bsm

FILEMODE=	755

LDLIBS += -lnsl -lbsm -lintl

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

clean:
	rm -rf $(OBJS) $(POS)

lint:	lint_PROG

include ../Makefile.targ
