#
#ident  "@(#)Makefile 1.1 94/07/13 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# libpthread

include		../../Makefile.master

SUBDIRS=	$(MACH)

all:=		TARGET=	all
install:=	TARGET=	install
clean:=		TARGET=	clean
clobber:=	TARGET=	clobber
lint:=		TARGET=	lint

.KEEP_STATE:

all clean clobber install lint:	\
		$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

links:
		-$(RM) $(MACH)
		$(SYMLINK) `mach` $(MACH)

FRC:
