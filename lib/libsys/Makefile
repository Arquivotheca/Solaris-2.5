#
#ident	"@(#)Makefile	1.5	95/02/24 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libsys/Makefile

include		../../Makefile.master

SUBDIRS=	$(MACH)

all:=		TARGET=	all
clean:=		TARGET=	clean
clobber:=	TARGET=	clobber
install:=	TARGET=	install
lint:=		TARGET=	lint

.KEEP_STATE:

all clean clobber install lint:	\
		$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
