#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)ttydefs.sh	1.12	93/04/20 SMI"	/* SVr4.0 1.2	*/

if u3b2 || sun || i386
then echo "# VERSION=1
38400:38400 hupcl:38400 hupcl::19200
19200:19200 hupcl:19200 hupcl::9600
9600:9600 hupcl:9600 hupcl::4800
4800:4800 hupcl:4800 hupcl::2400
2400:2400 hupcl:2400 hupcl::1200
1200:1200 hupcl:1200 hupcl::300
300:300 hupcl:300 hupcl::38400

38400E:38400 hupcl evenp:38400 evenp::19200
19200E:19200 hupcl evenp:19200 evenp::9600
9600E:9600 hupcl evenp:9600 evenp::4800
4800E:4800 hupcl evenp:4800 evenp::2400
2400E:2400 hupcl evenp:2400 evenp::1200
1200E:1200 hupcl evenp:1200 evenp::300
300E:300 hupcl evenp:300 evenp::19200

auto:hupcl:sane hupcl:A:9600

console:9600 hupcl opost onlcr:9600::console
console1:1200 hupcl opost onlcr:1200::console2
console2:300 hupcl opost onlcr:300::console3
console3:2400 hupcl opost onlcr:2400::console4
console4:4800 hupcl opost onlcr:4800::console5
console5:19200 hupcl opost onlcr:19200::console

contty:9600 hupcl opost onlcr:9600 sane::contty1
contty1:1200 hupcl opost onlcr:1200 sane::contty2
contty2:300 hupcl opost onlcr:300 sane::contty3
contty3:2400 hupcl opost onlcr:2400 sane::contty4
contty4:4800 hupcl opost onlcr:4800 sane::contty5
contty5:19200 hupcl opost onlcr:19200 sane::contty


4800H:4800:4800 sane hupcl::9600H
9600H:9600:9600 sane hupcl::19200H
19200H:19200:19200 sane hupcl::38400H
38400H:38400:38400 sane hupcl::2400H
2400H:2400:2400 sane hupcl::1200H
1200H:1200:1200 sane hupcl::300H
300H:300:300 sane hupcl::4800H

conttyH:9600 opost onlcr:9600 hupcl sane::contty1H
contty1H:1200 opost onlcr:1200 hupcl sane::contty2H
contty2H:300 opost onlcr:300 hupcl sane::contty3H
contty3H:2400 opost onlcr:2400 hupcl sane::contty4H
contty4H:4800 opost onlcr:4800 hupcl sane::contty5H
contty5H:19200 opost onlcr:19200 hupcl sane::conttyH
" >ttydefs
fi
