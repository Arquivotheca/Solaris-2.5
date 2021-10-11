#! /bin/sh
#
# @(#)adbgen.sh 1.18 92/11/01 SMI
#
case $1 in
-d)
	DEBUG=:
	shift;;
-*)
	flag=$1
	shift;;
esac
ADBDIR=/usr/lib/adb
PATH=$PATH:$ADBDIR
for file in $*
do
	if [ `expr "XX$file" : ".*\.adb"` -eq 0 ]
	then
		echo File $file invalid.
		exit 1
	fi
	if [ $# -gt 1 ]
	then
		echo $file:
	fi
	file=`expr "XX$file" : "XX\(.*\)\.adb"`
	if adbgen1 $flag < $file.adb > $file.adb.c
	then
		if ${CC:-cc} -w -D${ARCH:-`uname -m`} \
			-I/usr/share/src/uts/${ARCH:-`uname -m`} \
			-o $file.run $file.adb.c $ADBDIR/adbsub.o
		then
			$file.run | adbgen3 | adbgen4 > $file
			$DEBUG rm -f $file.run $file.adb.C $file.adb.c $file.adb.o
		else
			$DEBUG rm -f $file.run $file.adb.C $file.adb.c $file.adb.o
			echo compile failed
			exit 1
		fi
	else
		$DEBUG rm -f $file.adb.C
		echo adbgen1 failed
		exit 1
	fi
done
