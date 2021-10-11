#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)mk.rcS.d.sh	1.8	95/07/17 SMI"
#		/* SVr4.0 1.7.4.1	*/

sparc_STARTLST=""
i386_STARTLST="51pcicconfig"
MACH_STARTLST=`eval echo "\\$${MACH}_STARTLST"`

COMMON_STARTLST="30rootusr.sh 33keymap.sh 35cacheos.sh 40standardmounts.sh \
50drvconfig 60devlinks 70buildmnttab.sh 65pcmcia 10initpcmcia"

STOPLST="65pcmcia"

INSDIR=${ROOT}/etc/rcS.d

if [ ! -d ${INSDIR} ] 
then 
	mkdir ${INSDIR} 
	eval ${CH}chmod 755 ${INSDIR}
	eval ${CH}chgrp sys ${INSDIR}
	eval ${CH}chown root ${INSDIR}
fi 
for f in ${STOPLST}
do 
	name=`echo $f | sed -e 's/^..//' | sed -e 's/\.sh$//`
	rm -f ${INSDIR}/K$f
	ln ${ROOT}/etc/init.d/${name} ${INSDIR}/K$f
	eval ${CH}chmod 744 ${INSDIR}/K$f
	eval ${CH}chgrp sys ${INSDIR}/K$f
	eval ${CH}chown root ${INSDIR}/K$f
done
for f in ${COMMON_STARTLST} ${MACH_STARTLST}
do 
	name=`echo $f | sed -e 's/^..//' | sed -e 's/\.sh$//`
	rm -f ${INSDIR}/S$f
	ln ${ROOT}/etc/init.d/${name} ${INSDIR}/S$f
	eval ${CH}chmod 744 ${INSDIR}/S$f
	eval ${CH}chgrp sys ${INSDIR}/S$f
	eval ${CH}chown root ${INSDIR}/S$f
done
