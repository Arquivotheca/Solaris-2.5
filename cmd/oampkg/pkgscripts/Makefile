#
#ident	"@(#)Makefile	1.15	94/10/21 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/oampkg/pkgscripts/Makefile
#
PROG=		cmdexec
SHELLS=		r.build i.build i.sed i.awk r.sed r.awk i.CompCpio
ADMINFILE=	default
PRODUCT=	$(PROG) $(SHELLS)

SRCS=		$(PROG:%=%.c)

include ../../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN
#
# for messagin catalog file
#
POFILE= pkgscripts.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS += -a -x ../oampkg.xcl

LIBPKG=		-lpkg
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(LIBPKGDIR) $(CPPFLAGS.master)
LDLIBS=		$(LDLIBS.cmd) $(LIBPKG) -lintl

FILEMODE=	0555
OWNER=		root
GROUP=		sys
ROOTPKGADM=	$(ROOT)/var/sadm/install/admin
ROOTPKGSCR=	$(ROOT)/usr/sadm/install/scripts
ROOTPKGSCRPROG=	$(PROG:%=$(ROOTPKGSCR)/%)
ROOTPKGSCRSHELL=$(SHELLS:%=$(ROOTPKGSCR)/%)
ROOTPKGADMFILE= $(ADMINFILE:%=$(ROOTPKGADM)/%)

$(ROOTPKGADMFILE) := FILEMODE = 0444

.KEEP_STATE:

all: 		$(PRODUCT) $(ADMINFILE) 

catalog: $(POFILE)

$(POFILE):      $(POFILES)
	$(RM) 	$@
	cat     $(POFILES)      > $@

install:	all $(ROOTPKGSCRPROG) $(ROOTPKGSCRSHELL) $(ROOTPKGADMFILE)

$(ROOTPKGSCR)/%: %
	$(INS.file)

$(ROOTPKGADM)/%: %
	$(INS.file)

lint:		lint_SRCS

clean:

include ../../Makefile.targ
