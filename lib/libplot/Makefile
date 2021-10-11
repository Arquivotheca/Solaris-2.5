#
#ident	"@(#)Makefile	1.4	92/12/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libplot/Makefile
#
# include global definitions
include ../../Makefile.master

SUBDIRS= plot t300 t300s t4014 t450 vt0

all:=	  TARGET= all
clean:=   TARGET= clean
clobber:= TARGET= clobber
install:= TARGET= install
lint:=	  TARGET= lint

.KEEP_STATE:

all clean clobber install lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)


FRC:
