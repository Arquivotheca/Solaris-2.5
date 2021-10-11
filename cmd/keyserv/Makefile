#
#	Copyright (c) 1988-1992 Sun Microsystems Inc
#	All Rights Reserved.
#
#ident	"@(#)Makefile	1.18	95/04/28 SMI"
#
# cmd/keyserv/Makefile

SBINPROG = keyserv newkey
BINPROG	= keylogout keylogin domainname chkey

PROG= $(SBINPROG) $(BINPROG)

HDRS= key_prot.h

KEYSERVOBJS = keyserv.o
KEYLOGOUTOBJS = keylogout.o
KEYLOGINOBJS = keylogin.o
CHKEYOBJS = chkey.o
NEWKEYOBJS = newkey.o
UPDATEOBJS = update.o
DOMAINNAMEOBJS = domainname.o
K_OBJS = setkey.o detach.o key_generic.o
CHANGE_OBJS  = update.o chkey_common.o

OBJS	= $(KEYSERVOBJS) $(KEYLOGOUTOBJS) $(KEYLOGINOBJS) $(CHKEYOBJS) \
	$(NEWKEYOBJS) $(UPDATEOBJS) $(DOMAINNAMEOBJS) \
	$(K_OBJS)  $(CHANGE_OBJS)
SRCS	= $(OBJS:.o=.c)

SED=	sed

include ../Makefile.cmd

IBINPROG=		$(BINPROG:%=$(ROOTBIN)/%)
ISBINPROG=		$(SBINPROG:%=$(ROOTUSRSBIN)/%)

$(ROOTUSRSBIN)/keyserv \
$(ROOTUSRSBIN)/newkey :=			OWNER=		root
$(ROOTUSRSBIN)/keyserv \
$(ROOTUSRSBIN)/newkey :=			GROUP=		sys

$(ROOTBIN)/chkey :=				OWNER=		root
$(ROOTBIN)/chkey :=				GROUP=		sys
$(ROOTBIN)/chkey :=				FILEMODE=	4555

CPPFLAGS += -I.
LDLIBS += -lrpcsvc -lnsl -lmp


.KEEP_STATE:

all: $(PROG)

keyserv: $(K_OBJS)  $(KEYSERVOBJS)
	$(LINK.c) $(K_OBJS)  $(KEYSERVOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

keylogout: $(KEYLOGOUTOBJS)
	$(LINK.c) $(KEYLOGOUTOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

keylogin: $(KEYLOGINOBJS)
	$(LINK.c) $(KEYLOGINOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

chkey: $(CHANGE_OBJS)  $(CHKEYOBJS)
	$(LINK.c) $(CHANGE_OBJS)  $(CHKEYOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

newkey:$(CHANGE_OBJS)  $(NEWKEYOBJS)
	$(LINK.c) $(CHANGE_OBJS)  $(NEWKEYOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

update: $(UPDATEOBJS)
	$(LINK.c)  $(UPDATEOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

domainname: $(DOMAINNAMEOBJS)
	$(LINK.c) $(DOMAINNAMEOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(DIRS) $(IBINPROG) $(ISBINPROG)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

key_prot.h : $(ROOT)/usr/include/rpcsvc/key_prot.x
	$(RPCGEN) -h $(ROOT)/usr/include/rpcsvc/key_prot.x > key_prot.h

include ../Makefile.targ

