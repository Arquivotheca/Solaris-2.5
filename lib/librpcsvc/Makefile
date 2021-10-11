#
#ident	"@(#)Makefile	1.19	94/12/09 SMI"
#
# Copyright (c) 1993,1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/librpcsvc/Makefile
#
LIBRARY= librpcsvc.a
VERS = .1
PROTOCOL_DIR= $(ROOT)/usr/include/rpcsvc

OBJECTS= rstat_simple.o rstat_xdr.o rusers_simple.o rusersxdr.o rusers_xdr.o \
	 rwallxdr.o spray_xdr.o nlm_prot.o sm_inter_xdr.o \
	 bootparam_prot_xdr.o mount_xdr.o rpc_sztypes.o bindresvport.o

# include library definitions
include ../Makefile.lib

DERIVED_FILES= bootparam_prot_xdr.c rstat_xdr.c rusers_xdr.c spray_xdr.c \
	nlm_prot.c sm_inter_xdr.c mount_xdr.c rpc_sztypes.c

CLEANFILES += $(DERIVED_FILES)

CPPFLAGS += -DYP
LINTFLAGS=
LDLIBS += -lnsl -lc

LIBS += $(DYNLIB)

# definitions for install_h target
HDRS= 		$(TXTS)
ROOTHDRDIR=	$(ROOT)/usr/include/rpcsvc
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

.KEEP_STATE:

all: $(DERIVED_FILES) $(TXTS) $(LIBS)

install: all $(ROOTLIBS) $(ROOTLINKS)

install_h: $(DERIVED_FILES) $(ROOTHDRS)

check:	$(CHECKHDRS)

#
# Rules for building the derived files
#
# Derived files
#
bootparam_prot_xdr.c: $(PROTOCOL_DIR)/bootparam_prot.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/bootparam_prot.x > $@

rstat_xdr.c: $(PROTOCOL_DIR)/rstat.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/rstat.x > $@

rusers_xdr.c: $(PROTOCOL_DIR)/rusers.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/rusers.x > $@

sm_inter_xdr.c: $(PROTOCOL_DIR)/sm_inter.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/sm_inter.x > $@

spray_xdr.c: $(PROTOCOL_DIR)/spray.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/spray.x > $@

nlm_prot.c: $(PROTOCOL_DIR)/nlm_prot.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/nlm_prot.x > $@

mount_xdr.c: $(PROTOCOL_DIR)/mount.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/mount.x > $@

rpc_sztypes.c: $(PROTOCOL_DIR)/rpc_sztypes.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/rpc_sztypes.x > $@

# include library targets
include ../Makefile.targ
