#
#ident	"@(#)Makefile	1.7	94/12/09 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# src/cmd/prtdiag/Makefile
#
# builds occur in kernel-architecture subdirectories using Makefile.com
# in this directory.
#

SUBDIRS=	sun4d

# conditional assignments
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
test :=		TARGET= test
_msg :=		TARGET= _msg

include ../Makefile.cmd

.KEEP_STATE:

all install clean clobber lint test _msg: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
