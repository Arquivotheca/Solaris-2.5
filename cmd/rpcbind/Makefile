#
#ident	"@(#)Makefile	1.9	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/rpcbind/Makefile

PROG= rpcbind


OBJS= check_bound.o pmap_svc.o rpcb_svc.o rpcb_svc_com.o rpcb_svc_4.o \
	 rpcb_stat.o rpcbind.o warmstart.o
	

SRCS= $(OBJS:%.o=%.c)

include ../Makefile.cmd

CPPFLAGS= -I. -DPORTMAP $(CPPFLAGS.master)
LDLIBS += -lnsl -ldl

.KEEP_STATE:

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(DIRS) $(ROOTUSRSBINPROG)

$(DIRS):
	$(INS.dir)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

include ../Makefile.targ
