#
#ident	"@(#)Makefile.com	1.3	94/06/28 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#

LIBRARY=	libldmake.a
VERS=		.1

include		../../../../lib/Makefile.lib

ROOTLIBDIR=	$(ROOT)/opt/SUNWonld/lib

SGSPROTO=	../../proto/$(MACH)

COMOBJS=	ld_file.o lock.o

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
