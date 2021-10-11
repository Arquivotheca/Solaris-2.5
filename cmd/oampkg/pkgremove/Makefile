#
#ident	"@(#)Makefile	1.16	93/04/28 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/oampkg/pkgremove/Makefile
#
PROG=		pkgremove

OBJECTS=	main.o quit.o delmap.o check.o predepend.o
SRCS=		$(OBJECTS:.o=.c)

include ../../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN
#
# for messaging catalog
#
POFILE= pkgremove.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS += -a -x ../oampkg.xcl

LOCAL=		.
GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
LIBINST=	../libinst/libinst.a
LIBPKG=		-lpkg
LIBADM=		-ladm
LDLIBS=		$(LDLIBS.cmd) $(LIBINST) $(LIBPKG) $(LIBADM) -lintl

ROOTPKGBIN=	$(ROOT)/usr/sadm/install/bin
ROOTPKGBINPROG=	$(PROG:%=$(ROOTPKGBIN)/%)
FILEMODE=	0555
OWNER=		root
GROUP=		sys

.KEEP_STATE:

all: 		$(PROG)

$(PROG):	$(OBJECTS) $(LIBINST)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

$(LIBINST): FRC
	@if [ ! -f $(@D)/.target_done ]; then \
		cd $(@D); pwd; $(MAKE) $(TARGET); \
	fi
	
FRC:

install:	all $(ROOTPKGBINPROG)

$(ROOTPKGBIN)/%: %
	$(INS.file)

catalog: $(POFILE)

$(POFILE):      $(POFILES)
	$(RM) $@
	cat     $(POFILES)      > $@

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS)

include ../../Makefile.targ
