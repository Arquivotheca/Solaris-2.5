#
# ident	"@(#)Makefile	1.7	94/03/30 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/dfs.cmds/general/Makefile
#

PROG=	general

OBJS=	general.o

ROOTLINK= $(ROOTUSRSBIN)/unshare

CFLAGS += -s

include ../../Makefile.cmd

.KEEP_STATE:

all: $(PROG) 

$(PROG): general.o
	$(LINK.c) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRDBINPROG) $(ROOTLINK)

$(ROOTLINK): $(ROOTUSRSBINPROG)
	$(RM) $@; $(LN) $(ROOTUSRSBINPROG) $@

clean:
	$(RM) $(OBJS)

lint: lint_PROG

include ../../Makefile.targ

