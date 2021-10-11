#
#ident	"@(#)Makefile	1.7	95/03/15 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
# All Rights Reserved.
#
# cmd/localedef/Makefile

include		../Makefile.cmd

SUBDIRS = lib localedef \
		xsh4_charmap \
		xsh4_chrtbl xsh4_collate xsh4_message \
		xsh4_montbl xsh4_numeric xsh4_time

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=		TARGET= catalog

POFILE= localedef.po
POFILES= $(SUBDIRS:%=%/%.po)

.KEEP_STATE:

all lint clobber clean install: $(SUBDIRS)

_msg:	$(SUBDIRS)
	$(RM) $(POFILE)
	cat $(POFILES) > $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
