#
#ident "@(#)Makefile	1.13	94/07/01 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# uts/adb/Makefile
#
SUBDIRS=	$(MACH) platform

# conditional assignments
all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint

.KEEP_STATE:

all install clean clobber lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

