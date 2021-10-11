#
#ident	"@(#)Makefile	1.9	94/12/02 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libcurses/Makefile
#
# include global definitions
include ../../Makefile.master

SUBDIRS= screen		# demo . . . later

all:=		TARGET= all
clean:=		TARGET= clean
clobber:=	TARGET= clobber
install:=	TARGET= install
install_h:=	TARGET= install_h
lint:=		TARGET= lint
check:=		TARGET= check

.KEEP_STATE:

all clean clobber install lint: $(SUBDIRS)

install_h check:
	@cd screen; pwd; $(MAKE) $(TARGET)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

_msg:	$(MSGDOMAIN) catalog

catalog:
	cd screen; make _msg


FRC:

