#
# ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/eqn/Makefile
#

include ../Makefile.cmd

SUBDIR1 =       eqnchar.d
SUBDIR2 =                 neqn.d eqn.d
SUBDIRS =       $(SUBDIR1) $(SUBDIR2)




all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
strip :=	TARGET= strip

#
# for message catalog
#
_msg :=         TARGET= catalog
POFILE= eqn.po
POFILES= $(SUBDIR2:%=%/%.po)

.KEEP_STATE :

all :		$(SUBDIRS)

install clean clobber lint strip :	$(SUBDIRS)

$(POFILE):   $(SUBDIR2)
	$(RM)	$@
	cat     $(POFILES)      > $(POFILE)

$(SUBDIRS) :	FRC
		@cd $@; pwd; $(MAKE) $(TARGET)

FRC :

include ../Makefile.targ
