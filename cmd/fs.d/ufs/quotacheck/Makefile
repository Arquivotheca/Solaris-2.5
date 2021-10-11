#
#ident	"@(#)Makefile	1.8	92/08/26 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/ufs/quotacheck/Makefile

FSTYPE=		ufs
LIBPROG=	quotacheck
ATTMK=		$(LIBPROG)

OTHERINSTALL=	$(ROOTUSRSBIN)/$(LIBPROG)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(LIBPROG)

include		../../Makefile.fstype
LDLIBS	+=	-ladm -ldl
OBJS=		quotacheck.o
FSOBJS=		preenlib.o
FSSRCS=		../../preenlib.c
SRCS=		$(OBJS:.o=.c)

lint:		lint_PROG

$(FSOBJS): $(FSSRCS)
	$(COMPILE.c) -o $@ $(FSSRCS)

$(LIBPROG):	$(OBJS) $(FSOBJS)
		$(LINK.c) -o $@ $(OBJS) $(FSOBJS) $(LDLIBS)
		$(POST_PROCESS)

clean:
	-$(RM) $(OBJS) $(FSOBJS)

$(ROOTUSRSBIN)/$(LIBPROG):
		-$(RM) $@; $(SYMLINK) $(LINKVALUE) $@
