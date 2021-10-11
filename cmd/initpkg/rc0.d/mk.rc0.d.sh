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
#ident	"@(#)mk.rc0.d.sh	1.19	94/12/05 SMI"
#

STARTLST= 
STOPLST="00ANNOUNCE 42audit 50utmpd 55syslog 66nfs.server \
69autofs 70cron 75nfs.client 76nscd 85rpc"

INSDIR=${ROOT}/etc/rc0.d

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
	eval ${CH}chmod 755 ${INSDIR}/K$f
	eval ${CH}chgrp sys ${INSDIR}/K$f
	eval ${CH}chown root ${INSDIR}/K$f
done
