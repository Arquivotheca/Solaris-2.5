#
#ident	"@(#)Makefile	1.1	94/11/02 SMI"
#
# Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
#
# lib/fn/context/onc_ns/Makefile
#

SUBDIRS= files nis nisplus

install	:= TARGET = install
install_h := TARGET = install_h
all	:= TARGET = all
clean	:= TARGET = clean
clobber	:= TARGET = clobber
debug	:= TARGET = debug
tcov	:= TARGET = tcov
analyse	:= TARGET = analyse

all debug install install_h:  $(SUBDIRS)
clean clobber tcov analyse:  $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

.KEEP_STATE:
