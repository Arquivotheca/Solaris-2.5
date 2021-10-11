#
#ident	"@(#)Makefile	1.10	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ipc/Makefile
#

include ../Makefile.cmd

IPCRM= 		ipcrm
IPCS= 		ipcs

PROG=		$(IPCRM) $(IPCS)

SRCS=		$(PROG:%=%.c)

CPPFLAGS +=	-D_KMEMUSER
LINTFLAGS =	-x
ROOTIPCRM =	$(IPCRM:%=$(ROOTBIN)/%)
ROOTIPCS =	$(IPCS:%=$(ROOTBIN)/%)
LDLIBS +=	-lkvm -lelf

$(ROOTIPCS) :=	FILEMODE = 2555
$(ROOTIPCS) :=	GROUP = sys

.KEEP_STATE:

all: 		$(PROG) 

install:	all $(ROOTPROG)

clean:

lint:	lint_SRCS

include ../Makefile.targ
