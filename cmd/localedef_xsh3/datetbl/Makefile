#
#ident	"@(#)Makefile	1.11	93/09/01 SMI"	/* SVr4.0 1.1	*/
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#
# cmd/localedef/datetbl/Makefile

include ../../Makefile.cmd
include ../Makefile.locale

ROOTDATETBLDIR=	$(BUILDDIRS:%=$(ROOTLOCALE)/%/LC_TIME)
ROOTDATETBL=	$(ROOTDATETBLDIR:%=%/time)

ROOTDIRS +=	$(ROOTDATETBLDIR)
CLOBBERFILES=	dummy_clobber_file

.KEEP_STATE:

all:

install: all $(ROOTLOCALE) $(ROOTDIRS) $(ROOTSYMLN) $(ROOTDATETBL)

LC_TIME.%: datetbl.%.src
	$(RM) $@; cp $< $@

$(ROOTLOCALE)/%/LC_TIME/time: datetbl.%.src
	$(INS.rename)

clean:

include ../Makefile.target
include ../../Makefile.targ
