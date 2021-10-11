#
# Copyright (c) 1993-1994 by Sun Microsystems, Inc.
# All rights reserved.
#
#ident	"@(#)Makefile	1.9	94/12/30 SMI"
#
# cmd/ssa/Makefile
#
# Makefile for Sparc Storage Array (PLUTO)
#	Command line Interface and Library code
#

include ../Makefile.cmd

SUBDIRS= lib cli	# Note: do lib first

all:=           TARGET= all
install:=       TARGET= install
clean:=         TARGET= clean
clobber:=       TARGET= clobber
lint:=          TARGET= lint
browser:=	TARGET= browser

FIRMLIB  = $(ROOTLIB)/firmware/ssa
FIRMWARE = $(FIRMLIB)/ssafirmware

.KEEP_STATE:

all install clean clobber lint browser: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

install:	$(FIRMWARE)

$(FIRMWARE):	ssafirmware
	@if [ ! -d $(ROOTLIB)/firmware ]; then \
		install -d -m 0755 -u bin -g bin $(ROOTLIB)/firmware ; \
	fi
	@if [ ! -d $(FIRMLIB) ]; then \
		install -d -m 0755 -u bin -g bin $(FIRMLIB) ; \
	fi
	$(RM) $(FIRMWARE)
	install -s -m 0644 -u root -g other -f $(FIRMLIB) ssafirmware

FRC:
