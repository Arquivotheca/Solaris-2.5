#
#ident  "@(#)Makefile 1.5     93/10/05 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# cmd/backup/operd/Makefile
#

include ../Makefile.backup

SUBDIRS=	monitor operd

.KEEP_STATE:

all hsm debug: $(SUBDIRS)

install hsminstall clean clobber lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@$(DO_SUBDIR)

FRC:
