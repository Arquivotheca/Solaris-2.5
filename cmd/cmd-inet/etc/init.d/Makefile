#
#ident	"@(#)Makefile	1.5	92/07/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG1=	inetinit
PROG2=	inetsvc
PROG=	$(PROG1) $(PROG2)

include ../../../Makefile.cmd

STARTINET1= $(ROOTETC)/rc2.d/S69inet
STARTINET2= $(ROOTETC)/rc2.d/S72inetsvc

INITD= $(ROOTETC)/init.d
DIRS= $(INITD) $(ROOTETC)/rc2.d
FILEMODE= 0744
OWNER= root
GROUP= sys

ETCINITPROG=	$(PROG:%=$(INITD)/%)

.KEEP_STATE:

all: $(PROG)

install: all $(DIRS) $(ETCINITPROG) ln_PROG

$(INITD)/% : %
	$(INS.file)

ln_PROG :
	-$(RM) $(STARTINET1)
	$(LN) $(INITD)/$(PROG1) $(STARTINET1)
	-$(RM) $(STARTINET2)
	$(LN) $(INITD)/$(PROG2) $(STARTINET2)

$(DIRS):
	$(INS.dir)

clean:

lint:

include ../../../Makefile.targ
