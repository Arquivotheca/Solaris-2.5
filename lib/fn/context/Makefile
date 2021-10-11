#
#ident	"@(#)Makefile	1.9	95/01/29 SMI"
#
# Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/context/Makefile
#

SUBDIRS= onc_ns DNS fs printer x500 nisplus_root


install	:= TARGET = install
all	:= TARGET = all
clean	:= TARGET = clean
clobber	:= TARGET = clobber
debug	:= TARGET = debug
tcov	:= TARGET = tcov
analyse	:= TARGET = analyse

all debug install:  $(SUBDIRS)
clean clobber tcov analyse:  $(SUBDIRS)
install_h private_h check:

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

.KEEP_STATE:
