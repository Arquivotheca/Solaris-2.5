#
#ident	"@(#)Makefile	1.17	93/06/01 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/oampkg/installf/Makefile
#
INSTALLF=	installf

PROG=		$(INSTALLF)

OBJECTS=	main.o installf.o removef.o dofinal.o
SRCS=		$(OBJECTS:.o=.c)

include ../../Makefile.cmd
TEXT_DOMAIN= SUNW_OST_ADMIN
#
# for messaging
#
POFILE = $(INSTALLF).po
XGETFLAGS += -a -x ../oampkg.xcl

GENERAL=	../hdrs
LIBPKGDIR=	../../../lib/libpkg
CPPFLAGS=	-I$(GENERAL) -I$(LIBPKGDIR) $(CPPFLAGS.master)
LIBINST=	../libinst/libinst.a
LDLIBS=		$(LDLIBS.cmd) $(LIBINST) -lpkg -ladm  -lintl

OWNER=		root
GROUP=		sys

ROOTUSRSBINLINK=$(ROOTUSRSBIN)/removef

.KEEP_STATE:

all: 		$(PROG) 

$(PROG):	$(OBJECTS) $(LIBINST)
	$(LINK.c) -o $@ $(OBJECTS) $(LDLIBS)
	$(POST_PROCESS)

$(LIBINST): FRC
	@if [ ! -f $(@D)/.target_done ]; then \
		cd $(@D); pwd; $(MAKE) $(@F); \
	fi
	
FRC:
	
catalog:        $(POFILE)

$(POFILE):      $(SRCS)
	$(RM)	$@
	$(COMPILE.cpp) $(SRCS)   > $(POFILE).i
	$(XGETTEXT) $(XGETFLAGS) $(POFILE).i
	sed "/^domain/d"        messages.po > $@
	$(RM) messages.po $(POFILE).i

install:	all $(ROOTUSRSBINPROG) $(ROOTUSRSBINLINK)

$(ROOTUSRSBINLINK): $(ROOTUSRSBINPROG)
	$(RM) $@; $(LN) $(ROOTUSRSBINPROG) $@
 

clean:
	$(RM) $(OBJECTS)

lint:		lint_SRCS

include ../../Makefile.targ
