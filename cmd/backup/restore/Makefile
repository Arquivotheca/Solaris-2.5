#
# cmd/backup/restore/Makefile
#
# Copyright (c) 1983 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
#pragma ident	"@(#)Makefile	1.22	94/07/18 SMI"
#
#	CAUTION:  FSTYPE must be defined before including ../Makefile.backup,
#		  otherwise UTFROOTPKGUSRLIBFSTYPE doesn't get expanded
#		  properly and the magic doesn't happen.

FSTYPE=		ufs

include ../Makefile.backup

UFSRESTORE=	ufsrestore
HSMRESTORE=	hsmrestore

PROG=		$(UFSRESTORE)
PRODUCT=	$(PROG)
HSMPROG=	$(HSMRESTORE)
HSMPRODUCT=	$(HSMPROG)

OBJECTS=	dirs.o interactive.o main.o metamucil.o restore.o \
		symtab.o tape.o utilities.o
SRCS=		$(OBJECTS:%.o=%.c)

CLOBBERFILES=	$(PRODUCT) $(HSMPRODUCT) $(DEBUGPRODUCTS)

# A list of static libraries that use libdl_stubs which fakes the real
# dynamic linker by returning static symbols from the pre-linked libraries.
# This is used for ufsrestore only.
STATICLIBS=	-ldl_stubs -lnss_files -lnss_nis -lnss_nisplus \
		-lnss_compat -lnss_dns -lresolv -lsocket -lnsl -lstraddr

LOCAL=		.
GENERAL=	../include
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LIBDUMP=	../lib/$(DUMPLIB)
LDLIBS=		$(LDLIBS.cmd) $(LIBDUMP) -dn -lsocket -lnsl $(STATICLIBS) -lintl
HSMLDLIBS=	$(LDLIBS.cmd) $(LIBDUMP) -lsocket -lnsl -lintl

UFSROOTLINK=	$(UFSROOTUSRSBIN)/$(PROG)
LINKVALUE=	../lib/fs/$(FSTYPE)/$(PROG)

FILEMODE=	04555
OWNER=		root

.KEEP_STATE:

all:		$(PRODUCT) 

hsm:		$(HSMPRODUCT)

$(PROG):	$(OBJECTS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

$(HSMPROG):	$(OBJECTS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(OBJECTS) $(HSMLDLIBS)
	$(POST_PROCESS)

$$(LIBDUMP): FRC
	$(DO_LIBDIR)

FRC:

install:	all $(UFSROOTPKGUSRLIBFSTYPE) $(UFSROOTLINK)

hsminstall:	hsm $(HSMROOTPKGSBINPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

$(UFSROOTLINK):
	-$(RM) $@; $(SYMLINK) $(LINKVALUE) $(UFSROOTLINK)

include ../Makefile.targ
