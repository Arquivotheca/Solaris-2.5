#
#ident	"@(#)Makefile	1.12	95/08/23 SMI"
#
# Copyright (c) 1993-1995, by Sun Microsystems, Inc.
# All rights reserved.

include ../Makefile.com

DATAFILES +=	i.automaster i.cronroot i.deflogin i.defsu i.etcremote 	\
		i.group	i.inittab i.etcrpc i.etcsystem			\
		i.inetdconf i.iuap i.nsswitch i.netconfig i.passwd	\
		i.preserve i.renamenew i.syslogconf i.tiservices	\
		i.ttysrch i.tzonecvt i.etcprofile i.mailxrc

MACHDATAFILES = i.devlinktab i.drvalias i.kclasses i.minorperm i.nametomajor

.KEEP_STATE:

all: $(FILES) $(MACHDATAFILES)
install: all pkg

include ../Makefile.targ
