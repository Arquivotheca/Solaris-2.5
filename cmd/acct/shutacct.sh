#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)shutacct.sh	1.4	94/10/20 SMI"	/* SVr4.0 1.6	*/
#	"shutacct [arg] - shuts down acct, called from /usr/sbin/shutdown"
#	"whenever system taken down"
#	"arg	added to /var/wtmp to record reason, defaults to shutdown"
PATH=/usr/lib/acct:/usr/bin:/usr/sbin
_reason=${1-"acctg off"}
acctwtmp  "${_reason}"  >>/var/adm/wtmp
turnacct off
