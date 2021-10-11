#
#ident	"@(#)Makefile	1.2	93/11/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/sgs/lex/Makefile
#

include		../../../Makefile.master

SUBDIRS=	$(MACH)

all :=		TARGET= all
install :=	TARGET= install
install_lib :=	TARGET= install_lib
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint

.KEEP_STATE:

all clean clobber install install_lib lint:	$(SUBDIRS)

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
