#
#ident	"@(#)Makefile	1.2	90/08/19 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# ucblib/libdbm/Makefile
#
LIBRARY= libdbm.a

OBJECTS= dbm.o

# include library definitions
include ../../lib/Makefile.lib

LIBS += $(LINTLIB)

CPPFLAGS += -I$(ROOT)/usr/ucbinclude

ROOTLIBDIR=	$(ROOT)/usr/ucblib

LINTSRC= $(LINTLIB:%.ln=%)
ROOTLINTDIR= $(ROOTLIBDIR)
ROOTLINT= $(LINTSRC:%=$(ROOTLINTDIR)/%)

# install rule for lint library target
$(ROOTLINTDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(LIBS)

$(LINTLIB):= SRCS=llib-ldbm
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=

install: $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLINT)

# include library targets
include ../../lib/Makefile.targ
include ../Makefile.ucbtarg

