#
#ident	"@(#)Makefile	1.21	95/08/29 SMI"	/* SVr4.0 1.17 */
#
# Copyright (c) 1989-1995, by Sun Microsystems, Inc.
# All rights reserved.
#

FSTYPE=		nfs
TYPEPROG=	mountd
ATTMK=		$(TYPEPROG)

include		../../Makefile.fstype

OBJS= 		mountd.o issubdir.o sharetab.o fslib.o
SRCS=		$(OBJS:%.o=%.c)
LDLIBS +=		-lrpcsvc -lnsl -lintl -lbsm
CFLAGS += -o $@

$(TYPEPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:	lint_SRCS

clean:     
	$(RM) $(OBJS)

fslib.o: ../../fslib.c
	$(COMPILE.c) ../../fslib.c
