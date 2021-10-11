#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)rc2.sh	1.13	95/01/12 SMI"	/* SVr4.0 1.16.7.1	*/

#	"Run Commands" executed when the system is changing to init state 2,
#	traditionally called "multi-user".


#	Pickup start-up packages for mounts, daemons, services, etc.

PATH=/usr/sbin:/usr/bin
set `/usr/bin/who -r`
if [ x$9 = "xS" -o x$9 = "x1" ]
then
	echo 'The system is coming up.  Please wait.'
	BOOT=yes

elif [ x$7 = "x2" ]
then
	echo 'Changing to state 2.'
	if [ -d /etc/rc2.d ]
	then
		for f in /etc/rc2.d/K*
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

if [ x$9 != "x2" -a x$9 != "x3" -a -d /etc/rc2.d ]
then
	for f in /etc/rc2.d/S*
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

#if [ ! -s /etc/rc2.d/.ports.sem ]
#then
#	/sbin/ports
#	echo "ports completed" > /etc/rc2.d/.ports.sem
#fi

# Start historical section.
if [ "${BOOT}" = "yes" -a -d /etc/rc.d ]
then
	for f in `/usr/bin/ls /etc/rc.d`
	{
		if [ ! -s /etc/init.d/${f} ]
		then
			/sbin/sh /etc/rc.d/${f} 
		fi
	}
fi
# End historical section.

if [ "${BOOT}" = "yes" -a x$7 = "x2" ]
then
	echo 'The system is ready.'
elif [ x$7 = "x2" ]
then
	echo 'Change to state 2 has been completed.'
fi

