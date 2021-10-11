#
#ident "@(#)Makefile	1.14	94/10/03 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# stand/Makefile

include ../Makefile.master

SUBDIRS = lib

all install clean clobber lint: $(SUBDIRS)

all	:=	TARGET = all
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint
install	:=	TARGET = install

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
