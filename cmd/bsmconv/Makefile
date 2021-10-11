#
#ident  "@(#)Makefile 1.1     93/03/11 SMI"
#
# Makefile for bsmconv
#

PROG = bsmconv
SRCS = bsmconv.sh

include ../Makefile.cmd

ETCSECURITYFILES =	bsmconv
ROOTETCSECURITY =	$(ROOT)/etc/security
$(ROOTETCSECURITY) :=   DIRMODE = 0755
$(ROOTETCSECURITY) :=   OWNER = root
$(ROOTETCSECURITY) :=   GROUP = sys
ROOTETCSECURITYFILES =  $(ETCSECURITYFILES:%=$(ROOTETCSECURITY)/%)
$(ROOTETCSECURITYFILES) := FILEMODE = 0744
$(ROOTETCSECURITYFILES) := OWNER = root
$(ROOTETCSECURITYFILES) := GROUP = sys

all: $(PROG)

install: all $(ROOTETCSECURITY) $(ROOTETCSECURITYFILES)

$(ROOTETCSECURITY):
	$(INS.dir)

$(ROOTETCSECURITY)/%:%
	$(INS.file)

clean:
	$(RM) -f $(PROG)

clobber: clean

include ../Makefile.targ

.KEEP_STATE:
