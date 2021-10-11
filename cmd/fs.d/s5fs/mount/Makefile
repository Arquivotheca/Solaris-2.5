#
#ident "@(#)Makefile	1.3	95/07/25 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		s5fs
PROG=		mount
ATTMK= $(PROG)

include		../../Makefile.fstype

LDLIBS += -lintl
CPPFLAGS += -I../.. -o $@
OBJS=		$(PROG).o fslib.o
SRCS=		$(OBJS:%.o=%.c)

$(PROG) $(STATIC):	$(OBJS)
		$(LINK.c) $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:	lint_SRCS

clean:     
	-$(RM) $(OBJS)

fslib.o: ../../fslib.c
	$(COMPILE.c) ../../fslib.c
