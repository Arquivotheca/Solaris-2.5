#
#ident	"@(#)Makefile	1.2	94/09/29 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libkvm/platform/sparc/Makefile
#
SUBDIRS=	sun4c sun4d sun4m

# conditional assignments
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
test :=		TARGET= test

.KEEP_STATE:

all install clean clobber lint test: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

