#
#ident	"@(#)Makefile	1.15	93/04/28 SMI"$(SRCS)
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/oampkg/pkginfo/Makefile
#
PKGINFO=	pkginfo
PKGPARAM=	pkgparam
PKGNAME=	pkgname
PRODUCT=	$(PKGINFO) $(PKGPARAM) $(PKGNAME)

UBINPROG=	$(PKGINFO) $(PKGPARAM)
PKGBINPROG=	$(PKGNAME)

SRCS=		$(PRODUCT:%=%.c)

include ../../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN
#
# For messaging catalog
#
POFILE= pkginfo.po
XGETFLAGS +=-a -x ../oampkg.xcl

GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
LIBINST=	../libinst/libinst.a
LIBPKG=		-lpkg
LIBADM=		-ladm
LDLIBS= 	$(LDLIBS.cmd) $(LIBINST) $(LIBPKG) $(LIBADM) -lintl

GROUP=		sys
CLOBBERFILES=	$(PRODUCT)

ROOTPKGBIN=	$(ROOT)/usr/sadm/install/bin
ROOTPKGBINPROG=	$(PKGBINPROG:%=$(ROOTPKGBIN)/%)
ROOTUSRBINPROG=	$(UBINPROG:%=$(ROOTBIN)/%)

$(ROOTPKGBINPROG) := OWNER = root

.KEEP_STATE:

all: 		$(PRODUCT) $(SRCS)

$(SRCS):	$(LIBINST)

catalog:        $(POFILE)

$(POFILE):      $(SRCS) $(LIBINST)
	$(RM) $@
	$(COMPILE.cpp) $(SRCS) > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS) $(POFILE).i
	sed "/^domain/d" messages.po > $@
	$(RM) messages.po $(POFILE).i

install:	all $(ROOTUSRBINPROG) $(ROOTPKGBINPROG)

$(ROOTPKGBIN)/%: %
	$(INS.file)

$(LIBINST): FRC
	@if [ ! -f $(@D)/.target_done ]; then \
		cd $(@D); pwd; $(MAKE) $(TARGET); \
	fi

FRC:
	
lint:
	@for i in $(SRCS); \
	do \
		echo '--------------------------------------------------'; \
		echo $$i; \
		$(LINT.c) $$i; \
	done

clean:

include ../../Makefile.targ
