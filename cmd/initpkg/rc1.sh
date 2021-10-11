#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)rc1.sh	1.10	93/04/11 SMI"	SVr4.0 1.4.1.1

#
#	"Run Commands" executed when the system is changing to init state 1
#

PATH=/usr/sbin:/usr/bin

set `/usr/bin/who -r`
if [ $9 = "S" ]
then
	echo 'The system is coming up for administration.  Please wait.'
	BOOT=yes

elif [ $7 = "1" ]
then
	echo 'Changing to state 1.'
	if [ -d /etc/rc1.d ]
	then
		for f in /etc/rc1.d/K*
		{
			if [ -s ${f} ]
			then
				case ${f} in
					*.sh)	.	 ${f} ;;
					*)	/sbin/sh ${f} stop ;;
				esac
			fi
		}
	fi
fi

if [ -d /etc/rc1.d ]
then
	for f in /etc/rc1.d/S*
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

/usr/sbin/killall
/usr/sbin/sleep 5
/usr/sbin/killall 9

if [ "${BOOT}" = "yes" -a $7 = "1" ]
then
	echo 'The system is ready for administration.'
elif [ $7 = "1" ]
then
	echo 'Change to state 1 has been completed.'
fi

