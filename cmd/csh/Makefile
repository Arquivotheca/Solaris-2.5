#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)Makefile	1.23	95/02/22 SMI"	
#
# cmd/csh/Makefile
#
#		PROPRIETARY NOTICE (Combined)
#
#This source code is unpublished proprietary information
#constituting, or derived under license from AT&T's UNIX(r) System V.
#In addition, portions of such source code were derived from Berkeley
#4.3 BSD under license from the Regents of the University of
#California.
#
#
#
#		Copyright Notice 
#
#Notice of copyright on this source code product does not indicate 
#publication.
#
#	(c) 1986-1995, by Sun Microsystems, Inc
#	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
#	          All rights reserved.

#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley Software License Agreement
# specifies the terms and conditions for redistribution.
#
#
# C Shell with process control; VM/UNIX VAX Makefile
# Bill Joy UC Berkeley; Jim Kulp IIASA, Austria
#

include ../Makefile.cmd

ED= ed
#
# For message catalogue files
#
GREP= egrep
SED= sed
POFILE= csh.po
CLOBBERFILES += sh.tconst.h

all :=          TARGET = all
install :=      TARGET = install
clean :=        TARGET = clean
clobber :=      TARGET = clobber
lint :=         TARGET = lint

.KEEP_STATE:

all install clean clobber lint:	$(MACH)

#
# For message catalogue files
#
_msg: $(MSGDOMAIN) sh.tconst.h .WAIT $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

$(POFILE):
	$(RM) $@ messages.po
	$(SED) -e 's/printf/gettext/' -e 's/bferr/gettext/' \
		-e 's/seterr/gettext/' -e 's/error(/gettext(/' \
		`$(GREP) -l 'gettext|printf|bferr|seterr|error' *.c` | \
		$(XGETTEXT) $(XGETFLAGS) -
	$(SED) -e '/^# msg/d' -e '/^domain/d' messages.po > $@
	$(RM) messages.po

sh.tconst.h:
	$(RM) $@; $(ED) sh.tconst.c < make.sh.tconst.h.ed

clobber: local_clobber $(MACH)

local_clobber:
	$(RM) $(CLOBBERFILES)

$(MACH): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
