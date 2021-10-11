#!/sbin/sh
#	Copyright (c) 1991 Sun Microsystems Inc.
#
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.
#ident	"@(#)swapadd.sh	1.6	94/12/22 SMI"

PATH=/usr/sbin:/usr/bin
USAGE="Usage: swapadd [-12] [file_system_table]"
SWAP="/usr/sbin/swap"
MOUNT="/sbin/mount"
FSTAB="/etc/vfstab"

#	Check to see if there is an entry in the fstab for a
#	specified file and mount it.  This allows swap files
#	(e.g. nfs files) to be mounted before being added for swap.
#
checkmount() {
	while read rspecial rfsckdev rmountp rfstype rfsckpass rautomnt rmntopts
	do
		case ${rspecial} in
		'#'* | '')	#  Ignore comments, empty lines
				continue ;;
		'-')		#  Ignore no-action lines
				continue
		esac
		if [ "${rmountp}" = "$1" ] ; then
			if [ "$rmntopts" != "-" ]; then
				# Use mount options if any
				ROPTIONS="$rmntopts"
			else
				ROPTIONS="rw"
			fi
			$MOUNT -m -o $ROPTIONS ${rspecial} > /dev/null 2>&1
			if [ $? != 0 ] ; then
				echo Mount of ${rmountp} for swap failed
			else
				echo Mounting ${rmountp} for swap
			fi
			return
		fi
	done < $FSTAB
}

#
# get file system table name and make sure file exists
#
pass="2"	# default to checking for existing swap

while getopts 12 o
do
	case $o in
	1 | 2)	pass=$o
		;;
	\?)	echo $USAGE; exit 1;
		;;
	esac
done
shift `expr $OPTIND - 1`
if [ $# -gt 1 ] ; then
	echo $USAGE; exit 1;
fi
if [ $# -eq 1 ] ; then
	if [ $1 = "-" ] ; then
		FSTAB=""
	else
		FSTAB=$1
		if [ ! -s "$FSTAB" ] ; then
			echo "swapadd: file system table ($FSTAB) not found"
			exit 1
		fi
	fi
fi

#
# Read the file system table to find entries of file system type "swap".
# Add the swap device or file specified in the first column
#
exec < $FSTAB
while read special t1 t2 fstype t3 t4 t5 ; do
	case $special in
	'#'* | '')	#  Ignore comments, empty lines
		continue
		;;
	'-')		#  Ignore no-action lines
		continue
	esac

	if [ "$fstype" != "swap" ] ; then
		continue
	fi
	if [ ${pass} = "1" ] ; then
		#
		# pass 1 should handle adding the swap files that
		# are accessable immediately; block devices, files
		# in / and /usr, and direct nfs mounted files.
		#
		if [ ! -b ${special} ] ; then
			#
			# Read the file system table searching for mountpoints
			# matching the swap file about to be added.
			#
			# NB: This won't work correctly if the file to added
			# for swapping is a sub-directory of the mountpoint.
			# e.g.	swapfile-> servername:/export/swap/clientname
			# 	mountpoint-> servername:/export/swap
			#
			checkmount ${special}
		fi
		if [ -f ${special} -a -w ${special} -o -b ${special} ] ; then
			$SWAP -a ${special}
		fi
	else
		#
		# pass 2 should skip all the swap already added.
		# If something added earlier uses the same name
		# as something to be added later, the following test
		# won't work. This should only happen if parts of a particular
		# swap file are added or deleted by hand between invocations
		c=`$SWAP -l | grep -c '\<'${special}'\>'`
		if [ $c -eq 0 ]; then
			$SWAP -a ${special} 
		fi
	fi
done
