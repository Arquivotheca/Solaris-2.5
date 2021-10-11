#
#ident	"@(#)Makefile	1.23	94/08/12 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ypcmd/Makefile

BINPROG = ypmatch ypwhich ypcat
SBINPROG = makedbm mkkrbconf ypalias ypinit yppoll ypset
NETYPPROG = ypbind ypxfr
PROG=	$(BINPROG) $(SBINPROG) $(NETYPPROG)

include ../Makefile.cmd

# installed directories
NETSVC = $(ROOTLIB)/netsvc
NETYP = $(NETSVC)/yp

ROOTVAR_YP = $(ROOT)/var/yp
ROOTBINDING = $(ROOTVAR_YP)/binding
ROOTDIRS= $(NETSVC) $(NETYP) $(ROOTVAR_YP) $(ROOTBINDING)

LDLIBS.yp = -lnsl
#LDLIBS.yp = -Bstatic -lnsl -Bdynamic -ldl -Bstatic
LDLIBS += $(LDLIBS.yp)

YPFILES = aliases nicknames
ETCFILES =  publickey
ROOTYPFILES= $(YPFILES:%=$(ROOTVAR_YP)/%)
ROOTETCFILE= $(ETCFILES:%=$(ROOTETC)/%)

TXTS=	$(ETCFILES:%=net_files/%) $(YPFILES:%=net_files/%)

YPBINDOBJ= yp_b_svc.o yp_b_subr.o rpc_bootstrap.o getlist.o

COMMONOBJ= getlist.o yp_getalias.o

YPUPDOBJ = openchild.o ypupdated.o

YPXFROBJ = ypxfr.o ypxfrd_client.o ypxfrd_xdr.o

YPMATOBJ = ypmatch.o nick.o

YPWHIOBJ = ypwhich.o nick.o

YPCATOBJ = ypcat.o nick.o

NETYPOBJ = $(YPBINDOBJ) $(YPUPDOBJ) $(YPXFROBJ)
OBJS = $(NETYPOBJ) makedbm.o nick.o pong.o ypalias.o ypcat.o ypmatch.o \
	yppush.o ypwhich.o

BINSRC = $(BINPROG:=.c)

SBINSRC = $(SBINPROG:=.c)

NETYPSRC = $(NETYPOBJ:.o=.c)

YPBINDSRC = $(YPBINSOBJ:.o=.c)

YPUPDSRC = $(YPUPDOBJ:.o=.c)

YPXFRSRC = $(YPXFROBJ:.o=.c)

SRCS = $(BINSRC) $(SBINSRC) $(NETYPSRC)

CLEANFILES=	ypxfrd.h ypxfrd_xdr.c

IBINPROG=		$(BINPROG:%=$(ROOTBIN)/%)
ISBINPROG=		$(SBINPROG:%=$(ROOTUSRSBIN)/%)
INETYPPROG=		$(NETYPPROG:%=$(NETYP)/%)

#conditional assignments
ypalias.o :=	CPPFLAGS= -DMAIN $(CPPFLAGS.master)
yp_b_svc.o :=	CPPFLAGS += -DINIT_DEFAULT
$(IBINPROG) := OWNER= root
$(IBINPROG) := GROUP= other
$(ROOTVAR_YP)/Makefile := FILEMODE= 555
$(ROOTVAR_YP)/nicknames := FILEMODE= 644
# non-default file attributes to avoid conflict with rpcsrc
$(NETSVC) :=                                  OWNER=          root
$(NETSVC) :=                                  GROUP=          sys


all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

# install rules
$(ROOTVAR_YP)/% \
$(ROOTETC)/%: net_files/%
	$(INS.file)

$(NETYP)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(PROG) $(TXTS)

ypbind: $(YPBINDOBJ)
	$(LINK.c) $(YPBINDOBJ) -o $@ $(LDLIBS)
	$(POST_PROCESS)

makedbm ypalias: $$@.o $(COMMONOBJ)
	$(LINK.c) -o $@ $@.o $(COMMONOBJ) $(LDLIBS)
	$(POST_PROCESS)

ypxfr: $(YPXFROBJ)
	$(LINK.c) -o $@ $(YPXFROBJ) $(LDLIBS)
	$(POST_PROCESS)

ypmatch: $(YPMATOBJ)
	$(LINK.c) -o $@ $(YPMATOBJ) $(LDLIBS)
	$(POST_PROCESS)

ypwhich: $(YPWHIOBJ)
	$(LINK.c) -o $@ $(YPWHIOBJ) $(LDLIBS)
	$(POST_PROCESS)

ypcat: $(YPCATOBJ)
	$(LINK.c) -o $@ $(YPCATOBJ) $(LDLIBS)
	$(POST_PROCESS)

ypxfrd_xdr.c: ypxfrd.x
	$(RM) ypxfrd_xdr.c; $(RPCGEN) -C -c ypxfrd.x -o ypxfrd_xdr.c

ypxfrd.h: ypxfrd.x
	$(RM) ypxfrd.h; $(RPCGEN) -C -h ypxfrd.x -o ypxfrd.h

ypxfrd_xdr.o ypxfrd_client.o: ypxfrd.h

install: all $(PROG) $(ROOTDIRS) $(ROOTETCFILE) $(ROOTYPFILES) $(IBINPROG) \
	 $(ISBINPROG) $(INETYPPROG)

$(ROOTDIRS):
	$(INS.dir)

clean:
	$(RM) $(OBJS) $(CLEANFILES)

clobber: clean
	$(RM) $(PROG)

FRC:
