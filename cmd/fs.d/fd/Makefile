#
#ident	"@(#)Makefile	1.12	95/07/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		fd
ATTMK=		$(FSTYPE)
NULLPROG=	fsck
LIBPROG=	mount $(NULLPROG)
OBJS=		mount.o fslib.o

include		../Makefile.fstype

CPPFLAGS += -I.. -o $@
LDLIBS += -lintl
# for messaging catalog
#
POFILE= fd.po
POFILES= mount.po

ROOTLIBNULLPROG= $(NULLPROG:%=$(ROOTLIBFSTYPE)/%)

mount:		$(OBJS)
		$(LINK.c) $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:		lint_PROG

catalog:        $(POFILE)

$(POFILE):      $(POFILES)
	$(RM) $@
	cat $(POFILES)  > $@

$(NULLPROG):
	$(CP) /dev/null $(NULLPROG)

fslib.o: ../fslib.c
	$(COMPILE.c) ../fslib.c
