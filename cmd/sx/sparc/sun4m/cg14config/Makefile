#
# Copyright (c) 1993, 1995 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.7	95/01/04 SMI"
#
#	cmd/sx/sparc/sun4m/cg14config/Makefile
#

PROG=		cg14config
PLATFORM=	sun4m

include ../../../../Makefile.cmd
include ../../../../../Makefile.psm

LDLIBS	+= -lm

ROOTPSMPROG	= $(USR_PSM_SBIN_DIR)/$(PROG)

OWNER= root
GROUP= sys

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPSMPROG)

clean:

lint:	lint_PROG

include ../../../../Makefile.targ
include ../../../../../Makefile.psm.targ
