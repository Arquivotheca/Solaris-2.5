#
#ident	"@(#)Makefile	1.15	94/08/18 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# lib/libdl/sparc/Makefile

4VERS=		.0

include		../Makefile.com

# A symbolic link from /usr/4lib/libdl.so.1.0 is required for BCP.

4DYNLIB=	libdl.so.1$(4VERS)
ROOT4LIBDIR=	$(ROOT)/usr/4lib
ROOT4LINKS=	$(ROOT4LIBDIR)/$(4DYNLIB)
$(ROOT4LINKS):=	LIBLINKPATH = ../lib/
ROOTLINTLIB=    $(LINTLIB:%=$(ROOTLIBDIR)/%)

.KEEP_STATE:

all:		$(DYNLIB) $(LINTLIB)

install:	all $(ROOTDYNLIB) $(ETCDYNLIB) $(ROOTLINKS) $(ROOT4LINKS) \
		$(ROOTLINTLIB)

lint:		$(LINTLIB)

include		../Makefile.targ

$(ROOT4LINKS):	$(ROOTDYNLIB)
		$(INS.liblink)
