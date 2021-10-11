#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.6	94/09/16 SMI"
#
# lib/scheme/Makefile
#

SUBDIRS= am_scheme em_scheme pm_scheme sm_scheme xm_scheme

all:= 		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

.KEEP_STATE:

all clean clobber lint: $(SUBDIRS)

install: all $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
