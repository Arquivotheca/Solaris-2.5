#
#ident	"@(#)Makefile	1.12	95/07/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		ufs
PROG=		mount
ATTMK= $(PROG)

include		../../Makefile.fstype

LDLIBS += -lintl
CPPFLAGS += -I../.. -o $@
OBJS=		$(PROG).o realpath.o fslib.o
SRCS=		$(OBJS:%.o=%.c)

$(PROG) $(STATIC):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)


lint:	lint_SRCS

clean:     
	-$(RM) $(OBJS)

# for messaging catalog
#
POFILE= mount.po


catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM) $@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS)        $(POFILE).i
	sed "/^domain/d" messages.po >  $@
	$(RM) $(POFILE).i messages.po

fslib.o: ../../fslib.c
	$(COMPILE.c) ../../fslib.c
