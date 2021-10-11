#
#ident	"@(#)Makefile	1.6	94/08/01 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# lib/libelf/Makefile
#

include		../../Makefile.master

SUBDIRS=	$(MACH)

all:=		TARGET=	all
install:=	TARGET=	install
clean:=		TARGET=	clean
clobber:=	TARGET=	clobber
lint:=		TARGET=	lint

.KEEP_STATE:

all clean clobber install lint: \
		$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
