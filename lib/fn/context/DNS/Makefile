#
#ident	"@(#)Makefile	1.8 94/10/12 SMI"
#
# Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
#
# lib/fnsp/DNS/Makefile
#

LIBRARYCCC= fn_ctx_inet_domain.a
VERS = .1

#dns_names.o
OBJECTS= cx.o cx-hard.o ref.o glue.o dns_ops.o dns_obj.o


# include library definitions
include ../../Makefile.libfn

# do after include Makefile.lib, which also sets ROOTLIBDIR
ROOTLIBDIR=    $(ROOT)/usr/lib/fn

LIBS = $(DYNLIBCCC)

LDLIBS += -lxfn -lfn_spf -lresolv -lsocket -lnsl -lC -lc

.KEEP_STATE:

all: $(LIBS)

debug install tcov: all $(ROOTLIBDIR) $(ROOTLINKSCCC)

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../../Makefile.targ
