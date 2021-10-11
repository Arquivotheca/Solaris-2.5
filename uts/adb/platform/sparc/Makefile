#
#ident	"@(#)Makefile	1.4	95/02/14 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# uts/adb/platform/sparc/Makefile
#
SUBDIRS=	sun4c sun4d sun4m sun4u

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

