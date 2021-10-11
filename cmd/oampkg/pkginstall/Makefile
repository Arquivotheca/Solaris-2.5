#
#ident	"@(#)Makefile	1.16	93/12/20 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/oampkg/pkginstall/Makefile
#
PROG=		pkginstall

OBJECTS=	quit.o check.o dockspace.o cppath.o getinst.o \
		merginfo.o reqexec.o instvol.o main.o backup.o \
		sortmap.o pkgvolume.o predepend.o pkgenv.o
SRCS=		$(OBJECTS:.o=.c)

include ../../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN

#
# for messaging catalog file
#
POFILE= pkginstall.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS=-a -x ../oampkg.xcl

GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
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

catalog: $(POFILE)

$(POFILE):      $(POFILES)
	cat     $(POFILES)      > $@

$(LIBINST): FRC
	@if [ ! -f $(@D)/.target_done ]; then \
		cd $(@D); pwd; $(MAKE) $(TARGET); \
	fi
	
FRC:

install:	all $(ROOTPKGBINPROG)

$(ROOTPKGBIN)/%: %
	$(INS.file)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS)

include ../../Makefile.targ
