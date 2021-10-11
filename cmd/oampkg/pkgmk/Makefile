#
#ident	"@(#)Makefile	1.18	94/11/22 SMI"
#
# Copyright (c) 1990, 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/oampkg/pkgmk/Makefile
#
PROG=		pkgmk

OBJECTS=	splpkgmap.o main.o quit.o mkpkgmap.o
SRCS=		$(OBJECTS:.o=.c)

include ../../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN
#
# for messaging catalog file
#
POFILE = pkgmk.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS +=-a -x ../oampkg.xcl

GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
LIBINST=	../libinst/libinst.a
LIBPKG=		-lpkg
LIBADM=		-ladm
LDLIBS=		$(LDLIBS.cmd) $(LIBINST) $(LIBPKG) $(LIBADM) -lintl -ll

.KEEP_STATE:

all: 		$(PROG) 

$(PROG):	$(OBJECTS) $(LIBINST)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

catalog:        $(POFILE)

$(POFILE):      $(POFILES)
	$(RM) 	$@
	cat     $(POFILES)      > $@

$(LIBINST): FRC
	@if [ ! -f $(@D)/.target_done ]; then \
		cd $(@D); pwd; $(MAKE) $(TARGET); \
	fi
	
FRC:

install:	all $(ROOTPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS)

include ../../Makefile.targ
