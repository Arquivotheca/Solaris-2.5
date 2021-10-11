#
#ident	"@(#)Makefile	1.6	92/07/14 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

ETCFILES= motd

include ../../Makefile.cmd

FILEMODE= 0644
OWNER= root
GROUP= sys

ROOTETCFILES= $(ETCFILES:%=$(ROOTETC)/%)

.KEEP_STATE:

all: $(ETCFILES)

install: all mk_motd ins_motd

mk_motd: $(ETCFILES)
	$(MV) $(ETCFILES) $(ETCFILES).save
	sed 1d $(ETCFILES).save > $(ETCFILES)

ins_motd: $(ROOTETCFILES)
	$(RM) $(ETCFILES)
	$(MV) $(ETCFILES).save $(ETCFILES)

clean:

lint:

clobber:
