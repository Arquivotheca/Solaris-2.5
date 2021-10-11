#
#ident	"@(#)Makefile.com	1.2	94/08/30 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#

PROG=		pvs
SRCS=		../common/pvs.c

include		../../../Makefile.cmd

SGSPROTO=	../../proto/$(MACH)

MAPFILE=	../common/mapfile

CPPFLAGS +=	-I../../include -I../../include/$(MACH)
LDFLAGS +=	-Yl,$(SGSPROTO) -M $(MAPFILE)
LDLIBS +=	-lelf
LINTFLAGS +=	$(LDLIBS)

CLEANFILES +=	$(LINTOUT)
