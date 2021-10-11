#
#ident	"@(#)Makefile	1.7	94/11/17 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= unpack

include ../Makefile.cmd
LDLIBS += -lintl
CFLAGS += -v

XGETFLAGS += -a -x unpack.xcl
.KEEP_STATE:

all: $(PROG) 

pcat: $(PROG)

install: all $(ROOTPROG)
	-$(RM) $(ROOTBIN)/pcat
	$(SYMLINK) ./$(PROG) $(ROOTBIN)/pcat

clean:

lint:	lint_PROG

include ../Makefile.targ
