#
#ident	"@(#)Makefile	1.11	95/07/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		proc
ATTMK=		$(FSTYPE)
LIBPROG=	mount
OBJS=		mount.o fslib.o

include		../Makefile.fstype
#
# Message catalog
#
POFILE= mount.po

LDLIBS += -lintl
CPPFLAGS += -I.. -o $@

$(LIBPROG):	$(OBJS)
		$(LINK.c) $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:		lint_PROG
#
# message catalog
#
catalog: $(POFILE)
	$(RM) proc.po
	cp $(POFILE) proc.po

fslib.o: ../fslib.c
	$(COMPILE.c) ../fslib.c
