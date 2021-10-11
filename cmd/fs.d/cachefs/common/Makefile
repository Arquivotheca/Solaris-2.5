#
#ident	"@(#)Makefile	1.2	94/11/02 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		cachefs

include		../../Makefile.fstype

PROGOBJS=	subr.o stats_create.o stats_stats.o stats_log.o stats_dbm.o

include		../Makefile.cachefs

ARFLAGS=	rc
CLOBBERFILES += $(LIBRARY)

all:	$(LIBRARY)

$(LIBRARY):	$(OBJS)
	-$(RM) $(LIBRARY)
	$(AR) $(ARFLAGS) $(LIBRARY) $(OBJS)
