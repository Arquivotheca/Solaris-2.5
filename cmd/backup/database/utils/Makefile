#
#ident  "@(#)Makefile 1.20     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/database/utils/Makefile
#

include ../../Makefile.backup

DUMPDM=		dumpdm

HSMPROG=	$(DUMPDM)
PRODUCT=	$(HSMPROG)

DBSERVDIR=	../dbserv
DUMPDMOBJS=	mdbmaint.o dumpadd.o rebuilddir.o rebuildtape.o nreclaim.o \
		pslabel.o t_stdio.o
DUMPDBDOBJS=	dir_update.o instance_update.o tape_subr.o \
		trans_subr.o map_subr.o cache_subr.o
OBJECTS=	$(DUMPDMOBJS) $(DUMPDBDOBJS)
SRCS=		$(DUMPDMOBJS:%.o=%.c) $(DUMPDBDOBJS:%.o=$(DBSERVDIR)/%.c)

LOCAL=		.
GENERAL=	../../include
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LIBDUMP=	../../lib/$(DUMPLIB)
HSMLDLIBS=	$(LDLIBS.cmd) $(LIBDUMP) -lsocket -lnsl -lintl

CLOBBERFILES=	$(PRODUCT) $(DEBUGPRODUCTS)

FILEMODE=	0555

.KEEP_STATE:

all hsm:	$(PRODUCT) 

$(HSMPROG):	$(OBJECTS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(OBJECTS) $(HSMLDLIBS)
	$(POST_PROCESS)

debug:		$(DEBUGPRODUCT)

$$(LIBDUMP): FRC
	$(DO_LIBDIR)

FRC:

install hsminstall:	hsm $(HSMROOTPKGSBINPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

%.o: $(DBSERVDIR)/%.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<
	$(POST_PROCESS_O)

include ../../Makefile.targ

# XXX - ugly hack due to a make bug with:
#$(DEBUGDIR)/%.o: $(DBSERVDIR)/%.c
#	$(COMPILE.c) $(OUTPUT_OPTION) $<
#	$(POST_PROCESS_O)
#
# must list all of the dependencies manually.  Ugh.
#
$(DEBUGDIR)/dir_update.o: $(DBSERVDIR)/dir_update.c
	$(COMPILE.c) -o $@ $(DBSERVDIR)/dir_update.c
	$(POST_PROCESS_O)

$(DEBUGDIR)/instance_update.o: $(DBSERVDIR)/instance_update.c
	$(COMPILE.c) -o $@ $(DBSERVDIR)/instance_update.c
	$(POST_PROCESS_O)

$(DEBUGDIR)/tape_subr.o: $(DBSERVDIR)/tape_subr.c
	$(COMPILE.c) -o $@ $(DBSERVDIR)/tape_subr.c
	$(POST_PROCESS_O)

$(DEBUGDIR)/trans_subr.o: $(DBSERVDIR)/trans_subr.c
	$(COMPILE.c) -o $@ $(DBSERVDIR)/trans_subr.c
	$(POST_PROCESS_O)

$(DEBUGDIR)/map_subr.o: $(DBSERVDIR)/map_subr.c
	$(COMPILE.c) -o $@ $(DBSERVDIR)/map_subr.c
	$(POST_PROCESS_O)

$(DEBUGDIR)/cache_subr.o: $(DBSERVDIR)/cache_subr.c
	$(COMPILE.c) -o $@ $(DBSERVDIR)/cache_subr.c
	$(POST_PROCESS_O)
