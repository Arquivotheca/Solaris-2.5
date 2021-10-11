#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#
#	Copyright (c) 1991-1933, by Sun Microsystems, Inc.
#

#ident	"@(#)rcS.sh	1.27	94/10/22 SMI"	SVr4.0 1.5.12.1

#
# This file has the commands in the rcS.d directory
# Those commands are necessary to get the system to single
# user mode:
#	establish minimal network plumbing (for diskless and dataless)
#	mount /usr (if a separate file system)
#	set the system name
#	check the root (/) and /usr file systems
#	check and mount /var and /var/adm (if a separate file system)
#	mount pseudo file systems (/proc and /dev/fd)
#	if this is a reconfiguration boot, [re]build the device entries
#	check and mount other file systems to be mounted in single user mode
#

#
# Default definitions:
#
PATH=/usr/sbin:/usr/bin:/sbin
vfstab=/etc/vfstab
mnttab=/etc/mnttab
mntlist=
option=
otherops=

#
# Useful shell functions:
#

#
#	shcat file
#
# Simulates cat in sh so it doesn't need to be on the root filesystem.
#
shcat() {
        while test $# -ge 1
        do
                while read i
                do
                        echo "$i"
                done < $1
                shift
        done
}

#
#	readvfstab mount_point
#
# Scan vfstab for the mount point specified as $1. Returns the fields of
# vfstab in the following shell variables:
#	special		: block device
#	fsckdev		: raw device
#	mountp		: mount point (must match $1, if found)
#	fstype		: file system type
#	fsckpass	: fsck pass number
#	automnt		: automount flag (yes or no)
#	mntopts		: file system specific mount options.
# All fields are retuned empty if the mountpoint is not found in vfstab.
# This function assumes that stdin is already set /etc/vfstab (or other
# appropriate input stream).
#
readvfstab() {
	while read special fsckdev mountp fstype fsckpass automnt mntopts
	do
		case ${special} in
		'#'* | '')	#  Ignore comments, empty lines
				continue ;;
		'-')		#  Ignore no-action lines
				continue
		esac

		if [ "${mountp}" = "$1" ]
		then
			break
		fi
	done
}

#
#	checkmessage raw_device fstype mountpoint
#
# Simple auxilary routine to the shell function checkfs. Prints out
# instructions for a manual file system check before entering the shell.
#
checkmessage() {
	echo ""
	echo "WARNING - Unable to repair the $3 filesystem. Run fsck"
	echo "manually (fsck -F $2 $1). Exit the shell when"
	echo "done to continue the boot process."
	echo ""
}

#
#	checkfs raw_device fstype mountpoint
#
# Check the file system specified. The return codes from fsck have the
# following meanings.
#	 0 - file system is unmounted and okay
#	32 - file system is unmounted and needs checking (fsck -m only)
#	33 - file system is already mounted
#	34 - cannot stat device
#	36 - uncorrectable errors detected - terminate normally (4.1 code 8)
#	37 - a signal was caught during processing (4.1 exit 12)
#	39 - uncorrectable errors detected - terminate rightaway (4.1 code 8)
#	40 - for root, same as 0 (used here to remount root)
# Note that should a shell be entered and the operator be instructed to
# manually check a file system, it is assumed the operator will do the right
# thing. The file system is not rechecked.
#
checkfs() {
	# skip checking if the fsckdev is "-"
	if [ $1 = "-" ]
	then
		return
	fi

	# if fsck isn't present, it is probably because either the mount of
	# /usr failed or the /usr filesystem is badly damanged.  In either
	# case, there is not much to be done automatically.  Halt the system
	# with instructions to either reinstall or `boot -b'.

	if [ ! -x /usr/sbin/fsck ]
	then
		echo ""
		echo "WARNING - /usr/sbin/fsck not found.  Most likely the"
		echo "mount of /usr failed or the /usr filesystem is badly"
		echo "damaged.  The system is being halted.  Either reinstall"
		echo "the system or boot with the -b option in an attempt"
		echo "to recover."
		echo ""
		uadmin 2 0
	fi

	/usr/sbin/fsck -F $2 -m $1  >/dev/null 2>&1

	if [ $? -ne 0 ]
	then
		# Determine fsck options by file system type
		case $2 in
			ufs)	foptions="-o p"
				;;
			s5)	foptions="-y -t /tmp/tmp$$ -D"
				;;
			*)	foptions="-y"
				;;
		esac

		echo "The $3 file system ($1) is being checked."
		/usr/sbin/fsck -F $2 ${foptions} $1
	
		case $? in
			0|40)	# file system OK
				;;

			36|39)	# couldn't fix the file system - enter a shell
				checkmessage "$1" "$2" "$3"
				/sbin/sulogin < /dev/console
				echo "resuming system initialization"
				;;
	
		  	*)	# fsck determined reboot is necessary
				echo "*** SYSTEM WILL REBOOT AUTOMATICALLY ***"
				/sbin/uadmin 2 1
				;;
		esac
	fi
}

#
#	checkopt option option-string
#
#	Check to see if a given mount option is present in the comma
#	separated list gotten from vfstab.
#
#	Returns:
#	${option}       : the option if found the empty string if not found
#	${otherops}     : the option string with the found option deleted
#
checkopt() {
	option=
	otherops=
	if [ "$2" = "-" ]
	then
		return
	fi
	searchop="$1"
	set `echo $2 | /usr/bin/sed -e "s/,/ /g"`
	while [ $# -gt 0 ]
	do
		if [ "$1" = "${searchop}" ]
		then
			option="$1"
		else
			if [ "X${otherops}" = "X" ]
			then
				otherops="$1"
			else
				otherops="${otherops},$1"
			fi
		fi
		shift
	done
}

#
# Start here: If requested, attempt to fall through to sulogin without
# doing anything. This is a clear act of desperation.
#
if [ "${RB_NOBOOTRC}" = "YES" ]
then
	exit 0
fi

#
# Make the old, deprecated environment variable (_DVFS_RECONFIG) and the new
# supported environment variable (_INIT_RECONFIG) to be synonyms.  Set both
# if the file /reconfigure exists.  _INIT_RECONFIG is the offical, advertized
# way to identify a reconfiguration boot.  Note that for complete backwards
# compatibility the value "YES" is significant with _DVFS_RECONFIG.  The
# value associated with _INIT_RECONFIG is insignificant.  What is significant
# is only that the environment variable is defined.
#
if [ "${_DVFS_RECONFIG}" = "YES" -o -n "${_INIT_RECONFIG}" -o -f /reconfigure ]
then
	_DVFS_RECONFIG="YES"; export _DVFS_RECONFIG
	_INIT_RECONFIG="set"; export _INIT_RECONFIG
fi

#
#
#
if [ -d /etc/rcS.d ]
then
	for f in /etc/rcS.d/S*
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

#
# Clean up the /reconfigure file and sync the new entries to stable media.
#
if [ -n "${_INIT_RECONFIG}" ]
then
	if [ -f /reconfigure  ]
	then
		/usr/bin/rm -f /reconfigure
	fi
	/sbin/sync
fi
