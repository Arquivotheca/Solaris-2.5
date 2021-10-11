#
#ident	"@(#)Makefile	1.1	94/10/25 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# psm/stand/bootblks/hsfsbootblk/Makefile

include ../../../../Makefile.master

###SUBDIRS=	v7 v9
SUBDIRS=	v7

all install clean clobber lint: $(SUBDIRS)

all	:=	TARGET = all
clean	:=	TARGET = clean
clobber	:=	TARGET = clobber
lint	:=	TARGET = lint
install	:=	TARGET = install

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(MFLAGS) $(TARGET)

FRC:
