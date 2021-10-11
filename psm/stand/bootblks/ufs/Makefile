#
#ident	"@(#)Makefile	1.24	94/11/29 SMI"
#
# Copyright (c) 1989-1994, by Sun Microsystems, Inc.
# All rights reserved.
#
# psm/stand/bootblks/ufs/Makefile
#

SUBDIRS	= $(MACH)

all	:=	TARGET= all
install	:=	TARGET= install
clean	:=	TARGET= clean
clobber	:=	TARGET= clobber
lint	:=	TARGET= lint

.KEEP_STATE:

all install clean clobber lint : $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

#
# install targets
#
$(USR_SBIN): $(USR)
	-$(INS.dir.bin.bin)

$(USR_SBIN)/%:	% $(USR_SBIN)
	$(INS.file)
