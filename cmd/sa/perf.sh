#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)perf.sh	1.6	95/02/09 SMI"       /* SVr4.0 1.4 */

# Uncomment the following lines to enable system accounting. (Also
# see /var/spool/cron/crontabs/sys)

#MATCH=`who -r|grep -c "[234][	 ]*0[	 ]*[S1]"`
#if [ ${MATCH} -eq 1 ]
#then
#	su sys -c "/usr/lib/sa/sadc /var/adm/sa/sa`date +%d`"
#fi
