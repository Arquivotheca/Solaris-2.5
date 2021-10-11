#
#ident	"@(#)Makefile	1.8	95/01/29 SMI"
#
# Copyright (c) 1993 - 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/fn/address/Makefile
#

SUBDIRS= onc_fn_nisplus inet_ipaddress_string fn_link_addr \
	onc_fn_nis onc_fn_files onc_fn_printer_nisplus \
	onc_fn_printer_nis onc_fn_printer_files \
	x500 osi_paddr onc_fn_nisplus_root


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
