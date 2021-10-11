#
#ident	"@(#)Makefile	1.3	95/02/02 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All Rights Reserved.
#
# cmd/sgs/yacc/Makefile
#

include		../../../Makefile.master

SUBDIRS=	$(MACH)

CSRC=		common/y1.c common/y2.c common/y3.c common/y4.c

all :=		TARGET= all
install :=	TARGET= install
install_lib :=	TARGET= install_lib
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint

# for messaging catalog
#

POFILE= yacc.po

.KEEP_STATE:

all clean clobber install install_lib lint:	$(SUBDIRS)

catalog:	$(POFILE)

$(POFILE):	
	$(RM) $@
	$(XGETTEXT) $(XGETFLAGS) $(CSRC)
	sed "/^domain/d" < messages.po > $@
	$(RM) messages.po

$(SUBDIRS):	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
