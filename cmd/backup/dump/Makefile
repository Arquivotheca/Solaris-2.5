#
# cmd/backup/dump/Makefile
#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
#	@(#)Makefile 1.13 93/10/05 SMI; from UCB 5.4 5/28/86
#
#	dump.h			header file
#	dumpdatabase.c		update dump database
#	dumpfstab.c		reads fstab/vfstab, mtab/mnttab
#	dumpitime.c		reads /etc/dumpdates
#	dumplabel.c		volume label support
#	dumpmain.c		driver
#	dumponline.c		online dump support
#	dumpoptr.c		operator interface
#	dumptape.c		handles the mag tape and opening/closing
#	dumptraverse.c		traverses the file system
#	lftw.c			fixed version of ftw(3)
#	partial.c		partial filesystem dump support
#	unctime.c		undo ctime
#
# CPPFLAGS:
#	DEBUG			use local directory to find ddate and dumpdates
#	PARTIAL			enable partial filesystem dump support
#	TDEBUG			trace out the process forking
#	USG			build on a System V system
#
#	CAUTION: FSTYPE must be defined before including ../Makefile.backup,
#		 otherwise UFSROOTPKGUSRLIBFSTYPE doesn't get expanded
#		 properly and the magic doesn't happen.

FSTYPE=		ufs

include ../Makefile.backup

UFSDUMP=	ufsdump
HSMDUMP=	hsmdump

PROG=		$(UFSDUMP)
PRODUCT=	$(PROG)
HSMPROG=	$(HSMDUMP)
HSMPRODUCT=	$(HSMPROG)


OBJECTS=	dumpdatabase.o dumpfstab.o dumpitime.o dumplabel.o dumpmain.o \
		dumponline.o dumpoptr.o dumptape.o dumptraverse.o unctime.o \
		partial.o lftw.o
SRCS=		$(OBJECTS:.o=.c)

CLOBBERFILES=	$(PRODUCT) $(HSMPRODUCT) $(DEBUGPRODUCTS)

LOCAL=		.
GENERAL=	../include
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) -DPARTIAL $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LIBDUMP=	../lib/$(DUMPLIB)
LDLIBS=		$(LDLIBS.cmd) $(LIBDUMP) -lsocket -lnsl -ladm -lintl
# we need HSMLDLIBS for making hsmdump_g
HSMLDLIBS=	$(LDLIBS)

UFSROOTDUMPDATES=	$(UFSROOTETC)/dumpdates
UFSROOTLINK=		$(UFSROOTUSRSBIN)/$(PROG)
LINKVALUE=		../lib/fs/$(FSTYPE)/$(PROG)
LIBDIR=			$(UFSROOTUSRLIBFS)/$(FSTYPE)
$(UFSROOTDUMPDATES):=	FILEMODE=	0664
$(UFSROOTDUMPDATES):=	OWNER=		root
$(UFSROOTDUMPDATES):=	GROUP=		sys
$(LIBDIR):=		DIRMODE=	0755
$(LIBDIR):=		OWNER=		root
$(LIBDIR):=		GROUP=		sys

FILEMODE=	06555
OWNER=		root
GROUP=		tty

.KEEP_STATE:

all:		$(PRODUCT) 

hsm:		$(HSMPRODUCT)

$(PROG) $(HSMPROG):	$(OBJECTS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

$$(LIBDUMP): FRC
	$(DO_LIBDIR)

FRC:

install:	all $(LIBDIR) $(UFSROOTPKGUSRLIBFSTYPE) dumpdates \
		$(UFSROOTDUMPDATES) $(UFSROOTLINK)

hsminstall:	hsm $(HSMROOTPKGSBINPROG)

dumpdates:
	cp /dev/null dumpdates

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

$(LIBDIR):
	$(INS.dir)

$(UFSROOTLINK):
	-$(RM) $@; $(SYMLINK) $(LINKVALUE) $(UFSROOTLINK)

include ../Makefile.targ
