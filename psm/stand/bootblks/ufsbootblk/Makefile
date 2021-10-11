#
#ident	"@(#)Makefile	1.2	94/11/17 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# psm/stand/bootblks/ufsbootblk/Makefile

include ../../../../Makefile.master

SUBDIRS=	v7 v9

all install clean clobber lint: $(SUBDIRS)

all	:=	TARGET = all
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint
install	:=	TARGET = install

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
