#
#ident	"@(#)Makefile	1.18	93/04/19 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/fs.d/ufs/fsck/Makefile

FSTYPE=		ufs
LIBPROG=	fsck
ATTMK=		$(LIBPROG)

include		../../Makefile.fstype

FSCKOBJS=	main.o dir.o inode.o pass1.o pass1b.o pass2.o \
		pass3.o pass3b.o pass4.o pass5.o setup.o \
		utilities.o

UFSDIR=		../../../../uts/common/fs/ufs
UFSOBJS=	ufs_subr.o ufs_tables.o

OBJS=		$(FSCKOBJS) $(UFSOBJS)
SRCS=		$(OBJS:%.o=%.c)

LDLIBS += -lc2 -ladm

$(LIBPROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

%.o:	$(UFSDIR)/%.c
	$(COMPILE.c) $<

lint:	lint_SRCS

clean:     
	$(RM) $(OBJS)
