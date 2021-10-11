#
#ident  "@(#)Makefile 1.19     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/recover/Makefile
#

include ../Makefile.backup

RECOVER=	recover

HSMPROG=	$(RECOVER)
PRODUCT=	$(HSMPROG)

RECOVEROBJS=	main.o cmds.o args.o extract.o fulldump.o find.o \
		perms.o getdnode.o terminal.o cmdtab.o help.o
DUMPDBDOBJS=	dir_subr.o dnode_subr.o instance_subr.o cache_subr.o \
		header_subr.o dblib_read.o
OBJECTS=	$(RECOVEROBJS) $(DUMPDBDOBJS)
SRCS=		$(OBJECTS:%.o=%.c)

LOCAL=		.
GENERAL=	../include
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LIBDUMP=	../lib/$(DUMPLIB)
HSMLDLIBS=	$(LDLIBS.cmd) $(LIBDUMP) -lsocket -lnsl -lintl

CLOBBERFILES=	$(PRODUCT) $(DEBUGPRODUCTS)

FILEMODE=	04555
OWNER=		root

.KEEP_STATE:

all hsm: 	$(PRODUCT) 

$(HSMPROG):	$(OBJECTS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(OBJECTS) $(HSMLDLIBS)
	$(POST_PROCESS)

$$(LIBDUMP): FRC
	$(DO_LIBDIR)

FRC:

install hsminstall:	hsm $(HSMROOTPKGBINPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

include ../Makefile.targ
