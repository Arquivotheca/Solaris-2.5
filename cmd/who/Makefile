#
#ident	"@(#)Makefile	1.14	94/11/16 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= who
XPG4PROG= who
UTMPS= utmp utmpx
ROOTADM= $(ROOT)/var/adm
ROOTUTMPS= $(UTMPS:%=$(ROOTADM)/%)
SYMSETC= $(UTMPS:%=$(ROOTETC)/%)
CLOBBERFILES += $(UTMPS)

include ../Makefile.cmd

$(ROOTUTMPS):= FILEMODE= 644
$(ROOTUTMPS):= OWNER= root
$(ROOTUTMPS):= GROUP= bin

$(ROOTADM)/%: %
	$(INS.file)

.KEEP_STATE:

CFLAGS += -v
LDLIBS += -lintl
$(XPG4):= CPPFLAGS += -DXPG4

XGETFLAGS += -a -x who.xcl

all: $(PROG) $(UTMPS) $(XPG4)

$(UTMPS):
	touch $@

install: all $(ROOTPROG) $(ROOTUTMPS) $(SYMSETC) $(ROOTXPG4PROG)

$(SYMSETC):
	-$(RM) $@; $(SYMLINK) ../var/adm/$(@F) $@
	$(CH)@-$(CHOWN) root $@
	$(CH)@-$(CHGRP) bin $@

clean:

lint:	lint_PROG

include ../Makefile.targ
