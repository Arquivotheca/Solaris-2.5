#
#ident	"@(#)Makefile	1.5	93/05/27 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# lib/libkrb/test/Makefile

SBINPROG = 
BINPROG	= 
TESTPROG = sample_client sample_server simple_client simple_server

PROG= $(SBINPROG) $(BINPROG) $(TESTPROG)

KOBJS	= sample_client.o sample_server.o simple_client.o simple_server.o
#DEBUGOBJS = syslog_hack.o
OBJS	= $(KOBJS) $(DEBUGOBJS)
SRCS	= $(OBJS:.o=.c)

include ../../../cmd/Makefile.cmd

IBINPROG=		$(BINPROG:%=$(ROOTBIN)/%)
ISBINPROG=		$(SBINPROG:%=$(ROOTUSRSBIN)/%)

# default is to build tests with static libkrb
KLIBS = ../libkrb.a

# if building with dynamic libkrb, then reset library define
dynamic :=	KLIBS	 = -lkrb

LDLIBS += $(KLIBS) -lnsl -lsocket
CPPFLAGS += -I../krb -DSYSV
simple_client.o simple_server.o := CPPFLAGS += -DDEBUG
simple_client.o simple_server.o := CPPFLAGS += $(INTERFLAGS)

.KEEP_STATE:

all dynamic install: $(PROG)

sample_client: sample_client.o $(DEBUGOBJS)
	$(LINK.c) -o $@ sample_client.o $(DEBUGOBJS) $(LDLIBS)
	$(POST_PROCESS)

sample_server: sample_server.o $(DEBUGOBJS)
	$(LINK.c) -o $@ sample_server.o $(DEBUGOBJS) $(LDLIBS)
	$(POST_PROCESS)

simple_client: simple_client.o $(DEBUGOBJS)
	$(LINK.c) -o $@ simple_client.o $(DEBUGOBJS) $(LDLIBS)
	$(POST_PROCESS)

simple_server: simple_server.o $(DEBUGOBJS)
	$(LINK.c) -o $@ simple_server.o $(DEBUGOBJS) $(LDLIBS)
	$(POST_PROCESS)

clean:
	$(RM) $(OBJS)

include ../../../cmd/Makefile.targ
