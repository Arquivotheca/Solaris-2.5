#!/bin/sh
#
#ident	"@(#)installboot.sh	1.11	94/10/29 SMI"
#

away() {
	echo $2 1>&2
	exit $1
}

Usage="Usage: `basename $0` bootblk raw-device"

test $# -ne 2 && away 1 "$Usage"

BOOTBLK=$1
DEVICE=$2
test ! -f $BOOTBLK && away 1 "$BOOTBLK: File not found"
test ! -c $DEVICE && away 1 "$DEVICE: Not a character device"
test ! -w $DEVICE && away 1 "$DEVICE: Not writeable"

# label at block 0, bootblk from block 1 through 15
stderr=`dd if=$BOOTBLK of=$DEVICE bs=1b oseek=1 count=15 conv=sync 2>&1`
err=$? ; test $err -ne 0 && away $err "$stderr"
exit 0
