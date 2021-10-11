#
#pragma ident	"@(#)Makefile	1.1	94/12/05 SMI"
#
# Copyright (c) 1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/context/fs/Makefile
#

SUBDIRS= host


all	:= TARGET = all
debug	:= TARGET = debug
install	:= TARGET = install
clean	:= TARGET = clean
clobber	:= TARGET = clobber
tcov	:= TARGET = tcov
analyse	:= TARGET = analyse

all debug install clean clobber tcov analyse:  $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

.KEEP_STATE:
