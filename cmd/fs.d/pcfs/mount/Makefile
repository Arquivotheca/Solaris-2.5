#
#ident	"@(#)Makefile	1.7	95/07/25 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#

FSTYPE=		pcfs
LIBPROG=	mount
ATTMK=		$(LIBPROG)
OBJS=		mount.o fslib.o

include		../../Makefile.fstype

LDLIBS +=  -lintl
CPPFLAGS += -I../.. -o $@

$(LIBPROG):	$(OBJS)
		$(LINK.c) $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:		lint_PROG

fslib.o: ../../fslib.c
	$(COMPILE.c) ../../fslib.c
