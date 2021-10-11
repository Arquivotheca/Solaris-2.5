#
#ident	"@(#)Makefile.com	1.1	94/05/18 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#

PROG=		ldd
SRCS=		../common/ldd.c

include		../../../Makefile.cmd

CPPFLAGS +=	-I../../include -I../../include/$(MACH)
LDLIBS +=	-lelf 
LINTFLAGS +=	$(LDLIBS)

CLEANFILES +=	$(LINTOUT)
