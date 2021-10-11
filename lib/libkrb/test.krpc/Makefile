#
#ident	"@(#)Makefile	1.5	92/07/14 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# cmd/krpc-test/Makefile
# Makefile for sample test program to test kerberos-rpc

SBINPROG = 
BINPROG	= 
TESTPROG = ktest ktestd

PROG= $(SBINPROG) $(BINPROG) $(TESTPROG)

COBJS = ktest.o ktest_clnt.o
SOBJS = ktest_svc.o ktest_proc.o
COMOBJS = ktest_xdr.o

OBJS	= $(COBJS) $(SOBJS) $(COMOBJS)
SRCS	= $(OBJS:.o=.c)
RSRC	= ktest.h ktest_clnt.c ktest_svc.c ktest_xdr.c

include ../../../cmd/Makefile.cmd

IBINPROG=		$(BINPROG:%=$(ROOTBIN)/%)
ISBINPROG=		$(SBINPROG:%=$(ROOTUSRSBIN)/%)

# default is to build tests with static libkrb
KLIBS = ../libkrb.a

# if building with dynamic libkrb, then reset library define
dynamic :=	KLIBS	 = -lkrb

LDLIBS += $(KLIBS) -lnsl -lsocket
CPPFLAGS += -DDEBUG

.KEEP_STATE:

all dynamic: $(PROG)

ktestd: $(SOBJS) $(COMOBJS)
	$(LINK.c) $(SOBJS) $(COMOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

ktest: $(COBJS) $(COMOBJS)
	$(LINK.c) $(COBJS) $(COMOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

# Files derived by rpcgen

ktest.h:        ktest.x
	$(RM) $@
	$(RPCGEN) -h ktest.x > $@

ktest_clnt.c:   ktest.x
	$(RM) $@
	$(RPCGEN) -l ktest.x > $@

ktest_svc.c:    ktest.x
	$(RM) $@
	$(RPCGEN) -s udp -s tcp ktest.x | sed 's/svc_run();/svc_kerb_reg(NULL, TEST_NAME, "*", 0); svc_run();/' >  $@

ktest_xdr.c:    ktest.x
	$(RM) $@
	$(RPCGEN) -c ktest.x > $@

$(OBJS): ktest.h

install: all $(DIRS) $(IBINPROG) $(ISBINPROG)

clean:
	$(RM) $(OBJS) $(RSRC)

lint: lint_SRCS

include ../../../cmd/Makefile.targ
