#
#ident	"@(#)Makefile	1.14	93/05/26 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG=		uname
STATPROG=	uname

# duplicate ROOTSBIN value needed for installation rule
ROOTSBIN=	$(ROOT)/sbin

$(ROOTSBIN)/%: %.static
	$(INS.rename)
	 
include ../Makefile.cmd

CFLAGS += -Xa -v
LDLIBS += -lintl

CLOBBERFILES +=	$(STATIC)

.KEEP_STATE:

all: $(PROG) $(STATIC) 

install: all $(ROOTPROG) $(ROOTSBINPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
