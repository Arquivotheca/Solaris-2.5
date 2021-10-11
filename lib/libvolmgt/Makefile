#
#ident	"@(#)Makefile	1.8	94/10/28 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libvolmgt/Makefile
#
LIBRARY= libvolmgt.a
VERS=.1

LOC_OBJS= volattr.o volutil.o volprivate.o volname.o

OBJECTS= $(LOC_OBJS)

# include library definitions
include ../Makefile.lib

# hack SRCS to work for our reg stuff (overriding what is in ../Makefile.lib)
SRCS=$(LOC_OBJS:.o=.c)

LIBS += $(DYNLIB)

LINTFLAGS=	-u
LINTOUT=	lint.out
CLEANFILES=	$(LINTOUT) $(LINTLIB)

LDLIBS +=       -ladm

# definitions for install_h target
HDRS=		volmgt.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

$(ROOTHDRS) :=	FILEMODE= 644

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(LIBS) $(TXTS)

install_h: $(ROOTHDRS)

check: $(CHECKHDRS)

install: all $(ROOTLIBS) $(ROOTLINKS)

lint:	$(LINTLIB)

# include library targets
include ../Makefile.targ
