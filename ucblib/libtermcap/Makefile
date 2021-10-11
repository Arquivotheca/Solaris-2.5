#
#ident	"@(#)Makefile	1.4	94/03/29 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# ucblib/libtermcap/Makefile
#

LIBRARY= libtermcap.a

OBJECTS= termcap.o tgoto.o tputs.o

# include library definitions
include ../../lib/Makefile.lib

LIBS += $(LINTLIB)

ROOTLIBDIR=	$(ROOT)/usr/ucblib

LINTSRC= $(LINTLIB:%.ln=%)
ROOTLINTDIR= $(ROOTLIBDIR)
ROOTLINT= $(LINTSRC:%=$(ROOTLINTDIR)/%)

INC1= $(ROOT)/usr/ucbinclude

DEFS= -DCM_N -DCM_GT -DCM_B -DCM_D

CPPFLAGS.master += $(DEFS)

# conditional assignments
objs/termcap.o \
objs/tgoto.o  := CPPFLAGS = -I$(INC1) $(CPPFLAGS.master)

$(LINTLIB):= SRCS=llib-ltermcap
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=
$(LINTLIB):= CPPFLAGS += -I$(INC1)

# install rule for lint library target
$(ROOTLINTDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(LIBS)

install: all $(ROOTLIBS) $(ROOTLINT)

# include library targets
include ../../lib/Makefile.targ
include ../Makefile.ucbtarg
