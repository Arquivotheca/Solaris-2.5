#
#ident	"@(#)Makefile	1.2	91/08/13 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# cmd/kerbd/Makefile

KPROGS = kinit klist kdestroy ksrvtgt
SBINPROG = kerbd
BINPROG	= $(KPROGS)
TESTPROG = kerb 

PROG= $(SBINPROG) $(BINPROG) $(TESTPROG)

KERBDOBJS = kerbd.o kerbd_svc.o kerbd_proc.o key_generic.o
KERBCOBJS = kerb.o kerbd_clnt.o kerbd_handle.o
K_OBJS	= kerbd_xdr.o 

OBJS	= $(KERBDOBJS) $(KERBCOBJS) $(K_OBJS)
SRCS	= $(OBJS:.o=.c)
RSRC	= kerbd.h kerbd_clnt.c kerbd_svc.c kerbd_xdr.c

CLOBBERFILES += $(RSRC)

include ../Makefile.cmd

ROOTBINPROG=		$(BINPROG:%=$(ROOTBIN)/%)
ROOTSBINPROG=		$(SBINPROG:%=$(ROOTUSRSBIN)/%)

$(ROOTUSRSBIN)/kerbd :=	OWNER=	root
$(ROOTUSRSBIN)/kerbd :=	GROUP=	sys

LDLIBS += -lkrb -lnsl -lsocket

$(KPROGS) := CPPFLAGS += -DSYSV -DSunOS=50 

.KEEP_STATE:

all: $(PROG)

kerbd: $(K_OBJS) $(KERBDOBJS)
	$(LINK.c) $(KERBDOBJS) $(K_OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

kerb: $(K_OBJS) $(KERBCOBJS)
	$(LINK.c) $(KERBCOBJS) $(K_OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

#  Rules to generate derived rpcgen files from kerbd.x spec file.

kerbd.h:        kerbd.x
	$(RM) $@
	$(RPCGEN) -h kerbd.x > $@

kerbd_clnt.c:   kerbd.x
	$(RM) $@
	$(RPCGEN) -l kerbd.x > $@

kerbd_svc.c:    kerbd.x
	$(RM) $@
	$(RPCGEN) -m kerbd.x > $@

kerbd_xdr.c:    kerbd.x
	$(RM) $@
	$(RPCGEN) -c kerbd.x > $@

$(OBJS): kerbd.h

rpcgen: $(RSRC)

#  Local rules to generate kernel versions of xdr and header files.
#  These targets are made only when specifically requested.
#  The resulting files can then be moved to uts/common/rpc.

kernelfiles:	kerbd_prot.h kerbd_prot.c

kerbd_prot.h:	kerbd.h
	@$(RM) $@
	cp kerbd.h $@

kerbd_prot.c:	kerbd_xdr.c
	@$(RM) $@
	sed -e 's;#include..kerbd.h.;#include <rpc/kerbd_prot.h>;' \
		kerbd_xdr.c > $@

clean.kernelfiles:
	$(RM) kerbd_prot.h kerbd_prot.c


install: all $(DIRS) $(ROOTBINPROG) $(ROOTSBINPROG)

clean: clean.kernelfiles
	$(RM) $(OBJS)

lint: lint_SRCS

include ../Makefile.targ
