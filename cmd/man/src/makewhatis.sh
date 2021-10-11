#!/bin/sh -
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved
#
#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.
#

#ident	"@(#)makewhatis.sh	1.7	92/07/14 SMI"	/* SVr4.0 1.2	*/

#Notice of copyright on this source code product does not indicate 
#publication.
#
#	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
#	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
#	          All rights reserved.

trap "rm -f /tmp/whatisx.$$ /tmp/whatis$$; exit 1" 1 2 13 15

rm -f /tmp/whatisx.$$ /tmp/whatis$$

if test ! -d $1 ; then exit 1 ; fi

cd $1
top=`pwd`
for i in man?*
do
	if [ -d $i ] ; then
		cd $i
	 	if test "`echo *`" != "*" ; then
			/usr/lib/getNAME *
		fi
		cd $top
	fi
done >/tmp/whatisx.$$
sed  </tmp/whatisx.$$ \
	-e 's/\\-/-/' \
	-e 's/\\\*-/-/' \
	-e 's/ VAX-11//' \
	-e 's/\\f[PRIB0123]//g' \
	-e 's/\\s[-+0-9]*//g' \
	-e '/ - /!d' \
	-e 's/.TH [^ ]* \([^ 	]*\).*	\(.*\) -/\2 (\1)	 -/' \
	-e 's/	 /	/g' | \
awk '{	title = substr($0, 1, index($0, "- ") - 1)
	synop = substr($0, index($0, "- "))
	count = split(title, n, " ")
	for (i=1; i<count; i++) {
		if ( (pos = index(n[i], ",")) || (pos = index(n[i], ":")) )
			n[i] = substr(n[i], 1, pos-1)
		printf("%s\t%s %s\t%s\n", n[i], n[1], n[count], synop)
	}
}' >/tmp/whatis$$
/usr/bin/expand -16,32,36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100 \
	/tmp/whatis$$ | sort | /usr/bin/unexpand -a > windex
chmod 644 windex >/dev/null 2>&1
rm -f /tmp/whatisx.$$ /tmp/whatis$$
exit 0
