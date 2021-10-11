#
#ident	"@(#)Makefile	1.4	95/01/23 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
# All rights reserved.
#
# pkgdefs/SUNW0on/Makefile

include ../Makefile.com

ROOTDIR=$(ROOT)/usr/lib/locale/C
ROOTMSGDIR=$(ROOTDIR)/LC_MESSAGES
ROOTTIMEDIR=$(ROOTDIR)/LC_TIME

SED= sed
CP= cp

.KEEP_STATE:

all: $(FILES)
install: all pkg

include ../Makefile.targ

# The _msg target gathers the output of the top-level _msg target into
# text-domain-specific message files under the ROOTMSGDIR for packaging.
# It also processes time-message files in subdirectories of the LC_TIME
# directory and copies or creates other files needed by this package.
#
_msg:
	[ -d $(ROOTMSGDIR) ] || mkdir -p $(ROOTMSGDIR)
	[ -d $(ROOTTIMEDIR) ] || mkdir -p $(ROOTTIMEDIR)
	[ -d $(MSGROOT) ] || $(ECHO) "need to build _msg in $(SRC)"
	(cd $(MSGROOT); \
	for d in SUNW*;\
	do \
		$(SED) -e '/^# msg/d' -e '/^domain/d' $$d/*.po > \
			$(ROOTMSGDIR)/$$d.po; \
	done)
	(cd $(MSGROOT)/LC_TIME; \
	for d in SUNW*;\
	do \
		$(SED) -e '/^# msg/d' -e '/^domain/d' $$d/*.po > \
			$(ROOTTIMEDIR)/$$d.po; \
	done)
	-$(RM) $(ROOTMSGDIR)/mailx.help
	$(CP) $(SRC)/cmd/mailx/misc/mailx.help $(ROOTMSGDIR)
	-$(RM) $(ROOTMSGDIR)/more.help
	$(CP) $(SRC)/cmd/more/more.help $(ROOTMSGDIR)
	-$(RM) $(ROOTMSGDIR)/uxlibc.src
	$(CP) uxlibc.src $(ROOTMSGDIR)
	-$(RM) $(ROOTMSGDIR)/SUNW_OST_LINFO
	($(ECHO) "yes"; $(ECHO) "no" ) > $(ROOTMSGDIR)/SUNW_OST_LINFO
