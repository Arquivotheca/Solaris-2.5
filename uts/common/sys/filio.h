/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_FILIO_H
#define	_SYS_FILIO_H

#pragma ident	"@(#)filio.h	1.17	93/07/12 SMI"	/* SVr4.0 1.4	*/

/*	filio.h 1.3 88/02/08 SMI; from UCB ioctl.h 7.1 6/4/86	*/

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
 * General file ioctl definitions.
 */

#include <sys/ioccom.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	FIOCLEX		_IO('f', 1)		/* set exclusive use on fd */
#define	FIONCLEX	_IO('f', 2)		/* remove exclusive use */
/* another local */
#define	FIONREAD	_IOR('f', 127, int)	/* get # bytes to read */
#define	FIONBIO		_IOW('f', 126, int)	/* set/clear non-blocking i/o */
#define	FIOASYNC	_IOW('f', 125, int)	/* set/clear async i/o */
#define	FIOSETOWN	_IOW('f', 124, int)	/* set owner */
#define	FIOGETOWN	_IOR('f', 123, int)	/* get owner */

/*
 * ioctl's for Online: DiskSuite.
 * WARNING - the support for these ioctls may be withdrawn
 * in future OS releases.
 */
#define	_FIOLFS		_IO('f', 64)		/* file system lock */
#define	_FIOLFSS	_IO('f', 65)		/* file system lock status */
#define	_FIOFFS		_IO('f', 66)		/* file system flush */
#define	_FIOAI		_IO('f', 67)		/* get allocation info */
#define	_FIOSATIME	_IO('f', 68)		/* set atime */
#define	_FIOSDIO	_IO('f', 69)		/* set delayed io */
#define	_FIOGDIO	_IO('f', 70)		/* get delayed io */
#define	_FIOIO		_IO('f', 71)		/* inode open */
#define	_FIOISLOG	_IO('f', 72)		/* disksuite/ufs protocol */
#define	_FIOISLOGOK	_IO('f', 73)		/* disksuite/ufs protocol */
#define	_FIOLOGRESET	_IO('f', 74)		/* disksuite/ufs protocol */
#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FILIO_H */
