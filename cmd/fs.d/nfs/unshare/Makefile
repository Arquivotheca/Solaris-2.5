#
#ident	"@(#)Makefile	1.8	92/07/14 SMI"	/* SVr4.0 1.12	*/
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		nfs
LIBPROG=	unshare
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

OBJS= 		$(LIBPROG).o sharetab.o 
SRCS=		$(OBJS:%.o=%.c)
LDLIBS +=	-lnsl

$(LIBPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:	lint_SRCS

clean:     
	$(RM) $(OBJS)
