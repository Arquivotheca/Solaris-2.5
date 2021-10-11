#
#ident	"@(#)Makefile	1.4	92/09/06 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/man/Makefile

include ../Makefile.cmd
SUBDIRS = src

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=         TARGET= catalog

#for message catalog files
POFILE = man.po
POFILES = src/src.po

.KEEP_STATE:

all install clean lint: $(SUBDIRS)

clobber: $(SUBDIRS)	local_clobber

local_clobber:
	$(RM)	$(CLOBBERFILES)

_msg:   $(SUBDIRS)
	$(RM)	$(POFILE)
	cat $(POFILES)      > $(POFILE)
	$(RM)  $(MSGDOMAIN)/$(POFILE)
	cp $(POFILE) $(MSGDOMAIN)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
