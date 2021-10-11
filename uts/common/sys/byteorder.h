/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_BYTEORDER_H
#define	_SYS_BYTEORDER_H

#pragma ident	"@(#)byteorder.h	1.9	94/01/04 SMI"	/* SVr4.0 1.2 */

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc.
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

/*
 * macros for conversion between host and (internet) network byte order
 */

#if defined(_BIG_ENDIAN) && !defined(ntohl) && !defined(lint)
/* big-endian */
#define	ntohl(x)	(x)
#define	ntohs(x)	(x)
#define	htonl(x)	(x)
#define	htons(x)	(x)

#elif !defined(ntohl) /* little-endian */

unsigned short ntohs(unsigned short ns), htons(unsigned short hs);
unsigned long  ntohl(unsigned long nl), htonl(unsigned long hl);

#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BYTEORDER_H */
