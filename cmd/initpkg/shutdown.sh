#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#	Copyright (c) 1992, 1993, Sun Microsystems, Inc.

#ident	"@(#)shutdown.sh	1.14	94/10/13 SMI"	SVr4.0 1.8.3.2

#	Sequence performed to change the init state of a machine.  Only allows
#	transitions to states 0,1,5,6,s,S (i.e.: down or administrative states).

#	This procedure checks to see if you are permitted and allows an
#	interactive shutdown.  The actual change of state, killing of
#	processes and such are performed by the new init state, say 0,
#	and its /sbin/rc0.

usage() {
	echo "Usage: $0 [ -y ] [ -g<grace> ] [ -i<initstate> ] [ message ]"
	exit 1
}

notify() {
	/usr/sbin/wall <<-!
	$1
	!
	if [ -x /usr/sbin/showmount -a -x /usr/sbin/rwall ]
	then
		remotes=`/usr/sbin/showmount`
		if [ "X${remotes}" != "X" ]
		then
			/usr/sbin/rwall ${remotes} <<-!
			$1
			!
		fi
	fi
}

nologin=/etc/nologin

# Set the PATH so that to guarentee behavior of shell built in commands
# (such as echo).

PATH=/usr/sbin:/usr/bin:/sbin

# Initial sanity checks:
#	Make sure /usr is mounted
#	Check the user id (only root can run shutdown)

if [ ! -d /usr/bin ]
then
	echo "$0:  /usr is not mounted.  Mount /usr or use init to shutdown."
	exit 1
fi

if [ -x /usr/bin/id ]
then
	eval `/usr/bin/id  |  /usr/bin/sed 's/[^a-z0-9=].*//'`
	if [ "${uid:=0}" -ne 0 ]
	then
	        echo "$0:  Only root can run $0"
		exit 2
	fi
else
	echo "$0:  can't check user id."
	exit 2
fi

# Get options (defaults immediately below):

grace=60
askconfirmation=yes
initstate=s

while getopts g:i:y? c
do
	case $c in
	g)
		case $OPTARG in
		*[!0-9]* )
			echo "$0: -g requires a numeric option"
			usage
			;;
		[0-9]* )
			grace=$OPTARG
			;;
		esac
		;;
	i)
		case $OPTARG in
		[Ss0156])
			initstate=$OPTARG
			;;
		[234abcqQ])
			echo "$0: Initstate $OPTARG is not for system shutdown"
			exit 1
			;;
		*)
			echo "$0: $OPTARG is not a valid initstate"
			usage
			;;
		esac
		;;
	y)
		askconfirmation=
		;;
	\?)	usage
		;;
	esac
done
shift `expr $OPTIND - 1`

echo '\nShutdown started.    \c'
/usr/bin/date
echo

/sbin/sync
cd /

trap "rm $nologin >/dev/null 2>&1 ;exit 1"  1 2 15

# If other users are on the system (and any grace period is given), warn them.

for i in 7200 3600 1800 1200 600 300 120 60 30 10; do
	if [ ${grace} -gt $i ]
	then
		hours=`/usr/bin/expr ${grace} / 3600`
		minutes=`/usr/bin/expr ${grace} % 3600 / 60`
		seconds=`/usr/bin/expr ${grace} % 60`
		time=""
		if [ ${hours} -gt 1 ]
		then
			time="${hours} hours "
		elif [ ${hours} -eq 1 ]
		then
			time="1 hour "
		fi
		if [ ${minutes} -gt 1 ]
		then
			time="${time}${minutes} minutes "
		elif [ ${minutes} -eq 1 ]
		then
			time="${time}1 minute "
		fi
		if [ ${hours} -eq 0 -a ${seconds} -gt 0 ]
		then
			if [ ${seconds} -eq 1 ]
			then
				time="${time}${seconds} second"
			else
				time="${time}${seconds} seconds"
			fi
		fi

		notify \
"The system will be shut down in ${time}
$1"

		rm $nologin >/dev/null 2>&1
		cat > $nologin <<-!

			NO LOGINS: System going down in ${time}
			$1

		!

		/usr/bin/sleep `/usr/bin/expr ${grace} - $i`
		grace=$i
	fi
done

# Confirm that we really want to shutdown.

if [ ${askconfirmation} ]
then
	echo "Do you want to continue? (y or n):   \c"
	read b
	if [ "$b" != "y" ]
	then
		notify "False Alarm:  The system will not be brought down."
		echo 'Shutdown aborted.'
		exit 1
	fi
fi

# Final shutdown message, and sleep away the final 10 seconds (or less).

notify \
"THE SYSTEM IS BEING SHUT DOWN NOW ! ! !
Log off now or risk your files being damaged
$1"

if [ ${grace} -gt 0 ]
then
	/usr/bin/sleep ${grace}
fi

# Go to the requested initstate.

echo "Changing to init state $initstate - please wait"
/sbin/init ${initstate}
