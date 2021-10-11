#
#ident  "@(#)Makefile 1.5     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/database/Makefile
#

include ../Makefile.backup

SUBDIRS=	dbserv utils

.KEEP_STATE:

all hsm debug: $(SUBDIRS)

install hsminstall clean clobber lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@$(DO_SUBDIR)

FRC:
