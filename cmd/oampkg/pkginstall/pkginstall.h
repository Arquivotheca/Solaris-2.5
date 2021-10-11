/*	Copyright (c) 1994 SMI	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)pkginstall.h	1.2	94/10/21 SMI"

#ifndef __PKG_PKGINSTALL_H__
#define	__PKG_PKGINSTALL_H__

/* cppath() variables */
#define	DISPLAY		0x0001
#define	KEEPMODE	0x0002	/* mutex w/ SETMODE (setmode preempts) */
#define	SETMODE		0x0004	/* mutex w/ KEEPMODE */

/* special stdin for request scripts */
#define	REQ_STDIN	"/dev/tty"

#endif	/* __PKG_PKGINSTALL_H__ */
