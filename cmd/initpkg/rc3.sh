#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)rc3.sh	1.12	94/12/19 SMI"	SVr4.0 1.11.2.2

#	"Run Commands" executed when the system is changing to init state 3,
#	same as state 2 (multi-user) but with remote file sharing.

PATH=/usr/sbin:/usr/bin
set `/usr/bin/who -r`
if [ -d /etc/rc3.d ]
then
	for f in /etc/rc3.d/K*
	{
		if [ -s ${f} ]
		then
			case ${f} in
				*.sh)	.	 ${f} ;;	# source it
				*)	/sbin/sh ${f} stop ;;	# sub shell
			esac
		fi
	}

	for f in /etc/rc3.d/S*
	{
		if [ -s ${f} ]
		then
			case ${f} in
				*.sh)	.	 ${f} ;;	# source it
				*)	/sbin/sh ${f} start ;;	# sub shell
			esac
		fi
	}
fi

modunload -i 0 & > /dev/null 2>&1

if [ $9 = 'S' -o $9 = '1' ]
then
	echo 'The system is ready.'
fi

