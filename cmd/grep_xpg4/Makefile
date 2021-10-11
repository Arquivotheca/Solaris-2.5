#
#ident	"@(#)Makefile	1.12	95/03/07 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved
#

PROG= grep
XPG4PROG= grep 
FFILE= fgrep
EFILE= egrep

include ../Makefile.cmd

ROOTXPG4LINKS = $(ROOTXPG4BIN)/$(FFILE)	$(ROOTXPG4BIN)/$(EFILE)

CFLAGS += -v
LDLIBS += -lgen -lintl -lw
XGETFLAGS += -a -x grep.xcl

POFILE= grep_xpg4.po
POFILES= grep.po

.KEEP_STATE:

all: $(XPG4)  

install: all $(ROOTXPG4PROG) $(ROOTXPG4LINKS)

$(ROOTXPG4LINKS):	$(ROOTXPG4PROG)
	-$(RM) $@
	-$(LN) $(ROOTXPG4PROG) $@

$(POFILE): $(POFILES)
	$(RM) $@
	$(CP) $(POFILES) $@

# Make the links locally for test purposes
test:	$(FFILE) $(EFILE) 

$(FFILE):      $(XPG4PROG)
		@$(RM) $(FFILE); $(LN) $(XPG4PROG) $(FFILE)

$(EFILE):      $(XPG4PROG)
		@$(RM) $(EFILE); $(LN) $(XPG4PROG) $(EFILE)

clean:

lint:	lint_PROG

include ../Makefile.targ
