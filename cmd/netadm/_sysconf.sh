#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)_sysconf.sh	1.6	92/09/25 SMI"	/* SVr4.0 1.2	*/

if u3b2 || sun || i386
then echo "# This is the per-system configuration file
" >_sysconfig
fi
