#
#ident	"@(#)Makefile 1.4 92/12/07 SMI;"
#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
# Makefile for auditconfig

PROG= auditconfig

include ../Makefile.cmd

TEXT_DOMAIN=SUNW_BSM_AUDITCONFIG
POS=auditconfig.po
include ../Makefile.cmd.bsm

LDLIBS += -lnsl -lbsm -lintl

OBJS = auditconfig.o
SRCS = auditconfig.c

.KEEP_STATE:

all: $(PROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
