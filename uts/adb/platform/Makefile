#
#ident	"@(#)Makefile	1.1	93/11/18 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# uts/adb/platform/Makefile
#

SUBDIRS=	$(MACH)

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

