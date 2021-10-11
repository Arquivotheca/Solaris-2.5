#
#ident	"@(#)Makefile	1.5	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/setuname/Makefile
#

PROG= 		setuname

include ../Makefile.cmd

# Must include -lkvm since the program uses kvm_nlist and
# kvm_read/kvm_write on the running kernel
#
LIBELF=		-lelf
LIBKVM=		-lkvm
LDLIBS +=	$(LIBELF) $(LIBKVM) 

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)

clean:

lint:	lint_PROG

include ../Makefile.targ
