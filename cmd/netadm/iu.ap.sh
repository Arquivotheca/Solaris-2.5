#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)iu.ap.sh	1.16	95/08/07 SMI"	/* SVr4.0 1.3	*/

if u3b2
then echo "# /dev/console and /dev/contty autopush setup
#
# major	minor	lastminor	modules

    0	  -1	    0		ldterm
" >iu.ap
elif i386
then echo "# /dev/console and /dev/contty autopush setup
#
#       major minor   lastminor       modules

	chanmux	0	255	char ansi emap ldterm ttcompat
	asy	131072	131075	ldterm ttcompat
	asy	0	3	ldterm ttcompat
" > iu.ap
elif sun
then echo "# /dev/console and /dev/contty autopush setup
#
#      major   minor lastminor	modules

	wc	0	0	ldterm ttcompat
	zs	0	1	ldterm ttcompat
	zs	131072	131073	ldterm ttcompat
	ptsl	0	47	ldterm ttcompat
	mcpzsa	0	127	ldterm ttcompat
	mcpzsa	256	383	ldterm ttcompat
	stc	0	255	ldterm ttcompat
" >iu.ap
fi
