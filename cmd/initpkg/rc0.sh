#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)rc0.sh	1.16	94/11/30 SMI"	SVr4.0 1.15.4.1

#	"Run Commands" for init states 0, 5 and 6.

PATH=/usr/sbin:/usr/bin

echo 'The system is coming down.  Please wait.'

# make sure /usr is mounted before proceeding since init scripts
# and this shell depend on things on /usr file system
/sbin/mount /usr > /dev/null 2>&1

#	The following segment is for historical purposes.
#	There should be nothing in /etc/shutdown.d.
if [ -d /etc/shutdown.d ]
then
	for f in /etc/shutdown.d/*
	{
		if [ -s $f ]
		then
			/sbin/sh ${f}
		fi
	}
fi
#	End of historical section

if [ -d /etc/rc0.d ]
then
	for f in /etc/rc0.d/K*
	{
		if [ -s ${f} ]
		then
			case ${f} in
				*.sh)	.	 ${f} ;;	# source it
				*)	/sbin/sh ${f} stop ;;	# sub shell
			esac
		fi
	}

#	system cleanup functions ONLY (things that end fast!)	

	for f in /etc/rc0.d/S*
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

trap "" 15

# kill all processes, first gently, then with prejudice.
/usr/sbin/killall
/usr/bin/sleep 5
/usr/sbin/killall 9
/usr/bin/sleep 10
/sbin/sync; /sbin/sync; /sbin/sync

# unmount file systems. /usr, /var and /var/adm are not unmounted by umountall
# because they are mounted by rcS (for single user mode) rather than
# mountall.
# If this is changed, mountall, umountall and rcS should also change.
/sbin/umountall
/sbin/umount /var/adm >/dev/null 2>&1
/sbin/umount /var >/dev/null 2>&1
/sbin/umount /usr >/dev/null 2>&1

echo 'The system is down.'
