#
# @(#)Makefile	1.9 93/01/25   Copyright 1992 Sun Microsystems Inc.
#
# Makefile for the nis+ dns fwd daemon
#

SRVOBJS= main.o \
	ypresolv_proc.o \
	rpc_as.o \
	svc_run_as.o \
	ngethostbyname.o \
	nget_answer.o \
	nres_search.o \
	nres_rcv.o \
	nres_send.o \
	herror.o

COMMONOBJS= resolv_common.o

OBJS= $(SRVOBJS) $(COMMONOBJS)
SRCS= $(OBJS:.o=.c)

include $(SRC)/cmd/Makefile.cmd

PROG= rpc.nisd_resolv

# We don't use -lsocket for 4.x build
LDLIBS += -lnsl -lresolv -lsocket 

.KEEP_STATE:

all: $(PROG)

install: all $(ROOTUSRSBINPROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

resolv_common.o: ../resolv_common.c
	$(COMPILE.c) ../resolv_common.c

lint:
	$(LINT.c) $(SRVOBJS:.o=.c)

clobber:  clean
	$(RM) $(PROG)

clean:
	$(RM) $(OBJS)
