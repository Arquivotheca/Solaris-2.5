#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)mk.rc3.d.sh	1.12	93/04/08 SMI"
#		/* SVr4.0 1.7.2.1	*/

STARTLST="15nfs.server"
STOPLST= 

INSDIR=${ROOT}/etc/rc3.d

if [ ! -d ${INSDIR} ] 
then 
	mkdir ${INSDIR} 
	eval ${CH}chmod 755 ${INSDIR}
	eval ${CH}chgrp sys ${INSDIR}
	eval ${CH}chown root ${INSDIR}
fi 
for f in ${STARTLST}
do 
	name=`echo $f | sed -e 's/^..//'`
	rm -f ${INSDIR}/S$f
	ln ${ROOT}/etc/init.d/${name} ${INSDIR}/S$f
	eval ${CH}chmod 744 ${INSDIR}/S$f
	eval ${CH}chgrp sys ${INSDIR}/S$f
	eval ${CH}chown root ${INSDIR}/S$f
done
