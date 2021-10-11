#
#ident	"@(#)Makefile	1.10	95/07/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		hsfs
PROG=		mount
OBJS=		mount.o fslib.o
ATTMK= 		$(PROG)

include		../../Makefile.fstype

CPPFLAGS += -I../.. -o $@

#
# Message catalog
#
POFILE= mount.po

#
# message catalog
#
catalog: $(POFILE)

LDLIBS	+= -lintl

$(PROG) $(STATIC):	$(OBJS)
			$(LINK.c) $(OBJS) $(LDLIBS)
			$(POST_PROCESS)

fslib.o: ../../fslib.c
	$(COMPILE.c) ../../fslib.c
