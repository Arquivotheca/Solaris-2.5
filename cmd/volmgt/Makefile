# Copyright 1992, Sun Microsystems
#
# Makefile definitions for volume management
#
# "@(#)Makefile	1.2	92/09/29 SMI"
#
# cmd/volmgt/Makefile

SUBDIRS	= util vold rmm etc

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
cstyle :=	TARGET= cstyle

.KEEP_STATE:

all install cstyle lint clean clobber: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
