#
# ident	"@(#)Makefile	1.9	94/02/01 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/fs.d/nfs/statd/Makefile

FSTYPE=		nfs
TYPEPROG=	statd
ATTMK=		$(TYPEPROG)

include		../../Makefile.fstype

OBJS= 		sm_svc.o sm_proc.o sm_statd.o rpc.o
SRCS=		$(OBJS:%.o=%.c)
LDLIBS +=	-lrpcsvc -lnsl

$(TYPEPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

lint:		lint_SRCS

clean:
		$(RM) $(OBJS)

