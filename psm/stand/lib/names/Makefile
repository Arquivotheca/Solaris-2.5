#
#ident	"@(#)Makefile	1.2	94/12/10 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/lib/names/Makefile

include ../../../../Makefile.master

SUBDIRS = $(MACH)

all install clean clobber lint: $(SUBDIRS)

all	:=	TARGET = all
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint
install	:=	TARGET = install

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
