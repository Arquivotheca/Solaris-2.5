#
#ident	"@(#)Makefile	1.3	95/02/14 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/lib/names/sparc/Makefile

include ../../../../../Makefile.master

SUBDIRS = sun4c sun4m sun4d sun4u

all install clean clobber lint: $(SUBDIRS)

all	:=	TARGET = all
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint
install	:=	TARGET = install

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
