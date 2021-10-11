#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# lib/nsswitch/Makefile
#
# include global definitions
include $(SRC)/Makefile.master

SUBDIRS= files nis nisplus compat dns

all:=		TARGET= all
clean:=		TARGET= clean
clobber:=	TARGET= clobber
install:=	TARGET= install
lint:=		TARGET= lint

.KEEP_STATE:

all clean clobber install lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

