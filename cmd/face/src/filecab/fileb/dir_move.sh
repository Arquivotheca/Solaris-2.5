#!/sbin/sh
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)dir_move.sh	1.2	89/12/04 SMI"	/* SVr4.0 1.1	*/

. dir_copy "$@"
cd "$pwd"
rm -rf "$1"
