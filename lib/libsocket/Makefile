#
#ident	"@(#)Makefile	1.17	94/03/24 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libsocket/Makefile
#
LIBRARY= libsocket.a
VERS= .1

INETOBJS=  \
bindresvport.o  bootparams_getbyname.o byteorder.o     ether_addr.o \
getnetent.o     getnetent_r.o   getprotoent.o    getprotoent_r.o \
getservent.o    getservent_r.o	getservbyname_r.o \
inet_lnaof.o    inet_mkaddr.o   inet_network.o \
rcmd.o          rexec.o         ruserpass.o

SOCKOBJS=  \
_conn_util.o  _utility.o    accept.o      bind.o        connect.o  \
getpeernm.o   getsocknm.o   getsockopt.o  listen.o      receive.o  \
s_ioctl.o     send.o        setpeernm.o   setsocknm.o   setsockopt.o  \
shutdown.o    socket.o      socketpair.o  callselect.o

OBJECTS= $(INETOBJS) $(SOCKOBJS)

# libsocket build rules
objs/%.o profs/%.o pics/%.o: inet/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: socket/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

# include library definitions
include ../Makefile.lib

CPPFLAGS +=	-DSYSV -D_REENTRANT
%/rcmd.o :=	CPPFLAGS += -DNIS
LDLIBS +=	-lnsl


SRCS=	$(INETOBJS:%.o=inet/%.c) $(SOCKOBJS:%.o=socket/%.c)

LIBS += $(DYNLIB)

.KEEP_STATE:

all: $(LIBS)

install: $(LIBS) $(ROOTLIBS) $(ROOTLINKS)

# include library targets
include ../Makefile.targ

lint: llib-lsocket.ln
