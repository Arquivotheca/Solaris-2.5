#
#ident	"@(#)Makefile	1.10	93/09/27 SMI"	/* SVr4.0 1.12	*/
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#

FSTYPE=		nfs
LIBPROG=	share
ATTMK=		$(LIBPROG)

OBJS=		$(LIBPROG).o issubdir.o sharetab.o 
SRCS=		$(OBJS:%.o=%.c)
OTHERINSTALL=	$(ROOTETC)/dfs/fstypes

include		../../Makefile.fstype

LDLIBS +=	-lnsl

$(ROOTETC)/dfs/fstypes :=	FILEMODE= 644
$(ROOTETC)/dfs/fstypes :=	OWNER= root
$(ROOTETC)/dfs/fstypes :=	GROUP= root

$(ROOTETC)/dfs/%: %
	$(INS.file)

$(LIBPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:	lint_SRCS

clean:     
	$(RM) $(OBJS)
