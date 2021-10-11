#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)inittab.sh	1.27	94/05/05 SMI"	SVr4.0 1.18.6.1

if i386
then echo "ap::sysinit:/sbin/autopush -f /etc/iu.ap
fs::sysinit:/sbin/rcS			>/dev/console 2>&1 </dev/console
is:3:initdefault:
p3:s1234:powerfail:/usr/sbin/shutdown -y -i5 -g0	>/dev/console 2>&1
s0:0:wait:/sbin/rc0			>/dev/console 2>&1 </dev/console
s1:1:wait:/usr/sbin/shutdown -y -iS -g0	>/dev/console 2>&1 </dev/console
s2:23:wait:/sbin/rc2			>/dev/console 2>&1 </dev/console
s3:3:wait:/sbin/rc3			>/dev/console 2>&1 </dev/console
s5:5:wait:/sbin/rc5			>/dev/console 2>&1 </dev/console
s6:6:wait:/sbin/rc6			>/dev/console 2>&1 </dev/console
fw:0:wait:/sbin/uadmin 2 0		>/dev/console 2>&1 </dev/console
of:5:wait:/sbin/uadmin 2 6		>/dev/console 2>&1 </dev/console
rb:6:wait:/sbin/uadmin 2 1		>/dev/console 2>&1 </dev/console
sc:234:respawn:/usr/lib/saf/sac -t 300
co:234:respawn:/usr/lib/saf/ttymon -g -h -p \"\`uname -n\` console login: \" -T AT386 -d /dev/console -l console" \
>inittab

elif sun
then echo "ap::sysinit:/sbin/autopush -f /etc/iu.ap
fs::sysinit:/sbin/rcS			>/dev/console 2>&1 </dev/console
is:3:initdefault:
p3:s1234:powerfail:/usr/sbin/shutdown -y -i5 -g0 >/dev/console 2>&1
s0:0:wait:/sbin/rc0			>/dev/console 2>&1 </dev/console
s1:1:wait:/usr/sbin/shutdown -y -iS -g0	>/dev/console 2>&1 </dev/console
s2:23:wait:/sbin/rc2			>/dev/console 2>&1 </dev/console
s3:3:wait:/sbin/rc3			>/dev/console 2>&1 </dev/console
s5:5:wait:/sbin/rc5			>/dev/console 2>&1 </dev/console
s6:6:wait:/sbin/rc6			>/dev/console 2>&1 </dev/console
fw:0:wait:/sbin/uadmin 2 0		>/dev/console 2>&1 </dev/console
of:5:wait:/sbin/uadmin 2 6		>/dev/console 2>&1 </dev/console
rb:6:wait:/sbin/uadmin 2 1		>/dev/console 2>&1 </dev/console
sc:234:respawn:/usr/lib/saf/sac -t 300
co:234:respawn:/usr/lib/saf/ttymon -g -h -p \"\`uname -n\` console login: \" -T sun -d /dev/console -l console -m ldterm,ttcompat" \
>inittab

fi
