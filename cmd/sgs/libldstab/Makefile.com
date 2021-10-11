#
#ident	"@(#)Makefile.com	1.2	94/07/06 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#

LIBRARY=	libldstab.a
VERS=		.1

include		../../../../lib/Makefile.lib

ROOTLIBDIR=	$(ROOT)/usr/lib

SGSPROTO=	../../proto/$(MACH)

COMOBJS=	stab.o

OBJECTS=	$(COMOBJS)

MAPFILE=	../common/mapfile

DYNFLAGS +=	-Yl,$(SGSPROTO) -M $(MAPFILE)
CPPFLAGS=	-I../common -I../../include \
		-I../../include/$(MACH) $(CPPFLAGS.master)

CFLAGS +=	-K pic

SRCS=		$(OBJECTS:%.o=../common/%.c)
LDLIBS +=	-lelf

CLEANFILES +=	$(LINTOUT)
CLOBBERFILES +=	$(DYNLIB) $(LINTLIB)

ROOTDYNLIB=	$(DYNLIB:%=$(ROOTLIBDIR)/%)
