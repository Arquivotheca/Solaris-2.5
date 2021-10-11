#
#ident   "@(#)Makefile	1.1	94/12/14 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# uts/common/rpcsvc/Makefile
#	This makefile installs system header files that go into
#	/usr/include/rpcsvc.
#
# include global definitions
include ../../../Makefile.master

# Protocol descriptions.  Alas, the NFS protocol cannot be expressed
# completely via rpcgen.  The NLM description should go here some day.
RPCGEN_SRC=	sm_inter.x

DERIVED_HDRS=	$(RPCGEN_SRC:%.x=%.h)

ALLHDRS=	$(RPCGEN_SRC) $(DERIVED_HDRS)

ROOTDIRS=	$(ROOT)/usr/include/rpcsvc

ROOTHDRS=	$(ALLHDRS:%=$(ROOTDIRS)/%)

$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

# all_h permits derived headers to be built here in the uts source area
# for the kernel to reference, without going so far as to install them.
#
all_h: $(DERIVED_HDRS)

install_h: all_h $(ROOTDIRS) $(ROOTHDRS)

clean:
	$(RM) $(DERIVED_HDRS)

clobber: clean

# Don't check rpcgen-derived files.
check:

$(ROOTDIRS):
	$(INS.dir)

%.h: %.x
	$(RPCGEN) -C -h $< -o $@

