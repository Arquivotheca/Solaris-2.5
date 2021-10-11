#
#ident	"@(#)Makefile	1.16	93/04/28 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/oampkg/pkgtrans/Makefile
#
PROG=		pkgtrans

OBJECTS=	main.o
SRCS=		$(OBJECTS:.o=.c)

include ../../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN
#
# for messaging catalog
#
POFILE= pkgtrans.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS += -a -x ../oampkg.xcl

GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
LIBINST=	../libinst/libinst.a
LIBPKG=		-lpkg
LIBADM=		-ladm
LDLIBS=		$(LDLIBS.cmd) $(LIBINST) $(LIBPKG) $(LIBADM) -lintl

.KEEP_STATE:

all: 		$(PROG) 

$(PROG):	$(OBJECTS) $(LIBINST)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

catalog:        $(POFILE)

$(POFILE):      $(POFILES)
	$(RM)	$@
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
