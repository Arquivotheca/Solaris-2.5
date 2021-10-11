#
#ident  "@(#)Makefile 1.39     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/dumpex/Makefile
#

include ../Makefile.backup

DUMPCONFIG=	dumpconfig
DUMPCONFRC=	dumpconfrc
DUMPED=		dumped
DUMPEX=		dumpex
DUMPFIND=	dumpfind
DUMPSETUP=	dumpsetup
DUMPTM=		dumptm
TAPEDBFIX=	tapedbfix
DUMPDF=		dumpdf

HSMPROG=	$(DUMPCONFIG) $(DUMPED) $(DUMPEX) $(DUMPSETUP) $(DUMPTM) \
		$(TAPEDBFIX) $(DUMPDF)
SCRIPT=		$(DUMPCONFRC) $(DUMPFIND)
PRODUCT=	$(HSMPROG) $(SCRIPT)

DUMPCONFIGOBJS=	dumpconfig.o split.o globs.o subs.o xcursesubs.o \
		filesystems.o rsubs.o
DUMPEDOBJS=	dumped.o readit.o globs.o subs.o split.o checkget.o \
		dumpsubs1.o dumpsubs2.o rsubs.o cursesubs.o help.o \
		filesystems.o rsubs.o
DUMPEXOBJS=	dumpex.o readit.o split.o subs.o tapelib.o \
		updateconf.o dodump.o globs.o opermesg.o xcursesubs.o rsubs.o
DUMPTMOBJS=	dumptm.o tapelib.o subs.o globs.o split.o readit.o xcursesubs.o
DUMPSETUPOBJS=	dumpsetup.o
TAPEDBFIXOBJS=	tapedbfix.o readit.o globs.o subs.o split.o tapelib.o \
		xcursesubs.o
DUMPDFOBJS=	dumpdf.o

# -O objects (same as above)
$(DUMPCONFIG):=		POBJS=		$(DUMPCONFIGOBJS)
$(DUMPED):=		POBJS=		$(DUMPEDOBJS)
$(DUMPED):=		HSMLDLIBS +=	-lcurses -ltermlib
$(DUMPEX):=		POBJS=		$(DUMPEXOBJS)
$(DUMPTM):=		POBJS=		$(DUMPTMOBJS)
$(DUMPSETUP):=		POBJS=		$(DUMPSETUPOBJS)
$(TAPEDBFIX):=		POBJS=		$(TAPEDBFIXOBJS)
$(DUMPDF):=		POBJS=		$(DUMPDFOBJS)

# debugging objects (same as above)
$(DUMPCONFIG)_g:=	DEBUGOBJS=	$(DUMPCONFIGOBJS:%=$(DEBUGDIR)/%)
$(DUMPED)_g:=		DEBUGOBJS=	$(DUMPEDOBJS:%=$(DEBUGDIR)/%)
$(DUMPED)_g:=		HSMLDLIBS +=	-lcurses -ltermlib
$(DUMPEX)_g:=		DEBUGOBJS=	$(DUMPEXOBJS:%=$(DEBUGDIR)/%)
$(DUMPTM)_g:=		DEBUGOBJS=	$(DUMPTMOBJS:%=$(DEBUGDIR)/%)
$(DUMPSETUP)_g:=	DEBUGOBJS=	$(DUMPSETUPOBJS:%=$(DEBUGDIR)/%)
$(TAPEDBFIX)_g:=	DEBUGOBJS=	$(TAPEDBFIXOBJS:%=$(DEBUGDIR)/%)
$(DUMPDF)_g:=		DEBUGOBJS=	$(DUMPDFOBJS:%=$(DEBUGDIR)/%)

OBJECTS=	checkget.o cursesubs.o dodump.o dumpconfig.o dumpdf.o \
		dumped.o dumpex.o dumpsetup.o dumpsubs1.o dumpsubs2.o \
		dumptm.o filesystems.o globs.o help.o opermesg.o readit.o \
		rsubs.o split.o subs.o tapedbfix.o tapelib.o updateconf.o \
		xcursesubs.o
SRCS=		$(OBJECTS:.o=.c)

HSMROOTPKGLIBPROG=	$(DUMPSETUP:%=$(HSMROOTPKGLIB)/%) \
			$(DUMPDF:%=$(HSMROOTPKGLIB)/%)
HSMROOTPKGLIBSHFILES=	$(DUMPCONFRC:%=$(HSMROOTPKGLIB)/%)
HSMROOTPKGSBINPROG=	$(DUMPCONFIG:%=$(HSMROOTPKGSBIN)/%) \
			$(DUMPED:%=$(HSMROOTPKGSBIN)/%) \
			$(DUMPEX:%=$(HSMROOTPKGSBIN)/%) \
			$(DUMPTM:%=$(HSMROOTPKGSBIN)/%) \
			$(TAPEDBFIX:%=$(HSMROOTPKGSBIN)/%)
HSMROOTPKGSBINSHFILES=	$(DUMPFIND:%=$(HSMROOTPKGSBIN)/%)

LOCAL=		.
GENERAL=	../include
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) $(USG) $(ANSI) \
			-DHSMROOT=\"$(HSMROOT)\" $(CPPFLAGS.master)
LIBDUMP=	../lib/$(DUMPLIB)
HSMLDLIBS=	$(LDLIBS.cmd) $(LIBDUMP) -lsocket -lnsl -lintl

FILEMODE=	0555
CLOBBERFILES=	$(SCRIPT) $(PRODUCT) $(DEBUGPRODUCTS)

.KEEP_STATE:

all hsm:	$(PRODUCT) 

$(HSMPROG):	$$(POBJS) $$(LIBDUMP)
	$(LINK.c) -o $@ $(POBJS) $(HSMLDLIBS)
	$(POST_PROCESS)

$$(LIBDUMP): FRC
	$(DO_LIBDIR)

%: %.sed
	sed 's?XXXHSMROOTXXX?${HSMROOT}?' $< > $@

FRC:

install hsminstall:	hsm $(HSMROOTPKGLIBPROG) $(HSMROOTPKGLIBSHFILES) \
		$(HSMROOTPKGSBINPROG) $(HSMROOTPKGSBINSHFILES)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS) $(DEBUGOBJS)

include ../Makefile.targ
