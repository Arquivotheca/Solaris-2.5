#
#ident	"@(#)Makefile	1.6	93/04/28 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/oampkg/pkgmv/Makefile
#
PROG=		pkgmv

OBJECTS=	pkgmv.o
SRCS=		$(OBJECTS:.o=.c)

include ../../Makefile.cmd

TEXT_DOMAIN= SUNW_OST_ADMIN
#
# for messaging
#
POFILE = pkgmv.po
XGETFLAGS += -a -x ../oampkg.xcl

LOCAL=		.
GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(LOCAL) -I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
LIBINST=	../libinst/libinst.a
LIBPKG=		-lpkg
LIBADM=		-ladm
LDLIBS=		$(LDLIBS.cmd) $(LIBINST) $(LIBPKG) $(LIBADM) -lintl

FILEMODE=	0555
OWNER=		root
GROUP=		sys

.KEEP_STATE:

all: 		$(PROG) 

catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM)	$@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS) $(POFILE).i
	sed "/^domain/d"        messages.po > $@
	rm -f messages.po $(POFILE).i

$(PROG):	$(OBJECTS) $(LIBINST)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

$(LIBINST): FRC
	@if [ ! -f $(@D)/.target_done ]; then \
		cd $(@D); pwd; $(MAKE) $(TARGET); \
	fi
	
FRC:

install:	all $(ROOTUSRSBINPROG)

lint:		lint_SRCS

clean:
	$(RM) $(OBJECTS)

include ../../Makefile.targ
