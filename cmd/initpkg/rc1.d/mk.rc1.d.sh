#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
#ident	"@(#)mk.rc1.d.sh	1.14	94/12/05 SMI"
#

STARTLST="01MOUNTFSYS"

STOPLST="00ANNOUNCE 42audit 50utmpd 55syslog 65nfs.server \
67rpc 68autofs 70cron 76nscd 80nfs.client"

INSDIR=${ROOT}/etc/rc1.d

if [ ! -d ${INSDIR} ] 
then 
	mkdir ${INSDIR} 
	eval ${CH}chmod 755 ${INSDIR}
	eval ${CH}chgrp sys ${INSDIR}
	eval ${CH}chown root ${INSDIR}
fi 
for f in ${STOPLST}
do 
	name=`echo $f | sed -e 's/^..//'`
	rm -f ${INSDIR}/K$f
	ln ${ROOT}/etc/init.d/${name} ${INSDIR}/K$f
	eval ${CH}chmod 744 ${INSDIR}/K$f
	eval ${CH}chgrp sys ${INSDIR}/K$f
	eval ${CH}chown root ${INSDIR}/K$f
done
for f in ${STARTLST}
do 
	name=`echo $f | sed -e 's/^..//'`
	rm -f ${INSDIR}/S$f
	ln ${ROOT}/etc/init.d/${name} ${INSDIR}/S$f
	eval ${CH}chmod 744 ${INSDIR}/S$f
	eval ${CH}chgrp sys ${INSDIR}/S$f
	eval ${CH}chown root ${INSDIR}/S$f
done
