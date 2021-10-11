#
#ident	"@(#)Makefile	1.12	95/01/05 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# lib/libpkg/Makefile
#
LIBRARY= libpkg.a

OBJECTS= \
	canonize.o	ckparam.o	ckvolseq.o	cvtpath.o	\
	devtype.o	dstream.o	gpkglist.o	gpkgmap.o	\
	isdir.o		logerr.o	mappath.o	ncgrpw.o	\
	nhash.o		pkgexecl.o	pkgexecv.o	pkgmount.o	\
	pkgtrans.o	pkgxpand.o	ppkgmap.o	progerr.o	\
	putcfile.o	rrmdir.o	runcmd.o	srchcfile.o	\
	tputcfent.o	verify.o

# include library definitions
include ../Makefile.lib

CPPFLAGS=	-I. $(CPPFLAGS.master)
LINTFLAGS=	-u
LINTOUT=	lint.out
CLEANFILES=	$(LINTOUT) $(LINTLIB)
TEXT_DOMAIN= SUNW_OST_ADMIN

#
# for messaging catalog
POFILE = libpkg.po
POFILES= $(OBJECTS:%.o=%.po)

.KEEP_STATE:

all: $(LIBS) $(TXTS)

_msg:   $(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	cp $(POFILE) $(MSGDOMAIN)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat $(POFILES) > $@

install: all $(ROOTLIBS)

lint:	$(LINTLIB)

$(MSGDOMAIN):
	$(INS.dir)


# include library targets
include ../Makefile.targ
