#
#ident	"@(#)Makefile	1.14	95/07/25 SMI"	/* SVr4.0 1.	*/
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		nfs
LIBPROG=		umount
ATTMK= $(PROG)

include		../../Makefile.fstype

OBJS= 		$(LIBPROG).o fslib.o
SRCS=		$(OBJS:%.o=%.c)


CPPFLAGS += -I../.. -o $@

LDLIBS +=	-lrpcsvc -lnsl -lintl

#
# Message catalog
#
POFILE= umount.po

#
# message catalog
#
catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM) $@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS)        $(POFILE).i
	sed "/^domain/d"        messages.po     > $@
	$(RM) messages.po $(POFILE).i


$(LIBPROG):	$(OBJS)
		$(LINK.c)  -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:	lint_SRCS

clean:     
	$(RM) $(OBJS)

fslib.o: ../../fslib.c 
	$(COMPILE.c) ../../fslib.c

