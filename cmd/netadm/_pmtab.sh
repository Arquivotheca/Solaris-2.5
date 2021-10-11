#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)_pmtab.sh	1.3	92/09/25 SMI"	/* SVr4.0 1.2	*/

if u3b2 || sun || i386
then echo "# VERSION=1
ttya:u:root:reserved:reserved:reserved:/dev/term/a:I::/usr/bin/login::9600:ldterm,ttcompat:ttya login\: ::tvi925:y:# 
ttyb:u:root:reserved:reserved:reserved:/dev/term/b:I::/usr/bin/login::9600:ldterm,ttcompat:ttyb login\: ::tvi925:y:# " > _pmtab
fi
