#
#ident	"@(#)Makefile	1.17	95/02/15 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/oampkg/libinst/Makefile
#
LIBRARY=	libinst.a

OBJECTS=copyf.o		dockdeps.o	echo.o		eptstat.o	\
	finalck.o	findscripts.o	fixpath.o	flex_dev.o	\
	isreloc.o	lockinst.o	mntinfo.o	nblk.o		\
	ocfile.o	pathdup.o	pkgdbmerg.o	procmap.o	\
	psvr4ck.o	ptext.o		putparam.o	qreason.o	\
	qstrdup.o	setadmin.o	setlist.o	srcpath.o	\
	scriptvfy.o	stub.o		doulimit.o

# include library definitions
include ../../../lib/Makefile.lib

TEXT_DOMAIN= SUNW_OST_ADMIN
#
# messaging catalog
#
POFILE = libinst.po
POFILES= $(OBJECTS:%.o=%.po)
XGETFLAGS += -a -x ../oampkg.xcl

GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
LINTOUT=	lint.out
CLEANFILES=	$(LINTOUT) $(LINTLIB)

.KEEP_STATE:

all: 		$(LIBS) $(TXTS)

catalog: $(POFILE)

$(POFILE): $(POFILES)
	$(RM)	$@
	cat $(POFILES)  > $@

install:	all

lint:		$(LINTLIB)

# include library targets
include ../../../lib/Makefile.targ
