/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_DES_H
#define	_SYS_DES_H

#pragma ident	"@(#)des.h	1.10	93/11/01 SMI"	/* SVr4.0 1.2	*/

/*  des.h 2.7 88/02/08 SMI  */

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
 * Generic DES driver interface
 * Keep this file hardware independent!
 */

#include <sys/ioccom.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	DES_MAXLEN 	65536	/* maximum # of bytes to encrypt  */
#define	DES_QUICKLEN	16	/* maximum # of bytes to encrypt quickly */

enum desdir { ENCRYPT, DECRYPT };
enum desmode { CBC, ECB };

/*
 * parameters to ioctl call
 */
struct desparams {
	u_char des_key[8];	/* key (with low bit parity) */
	enum desdir des_dir;	/* direction */
	enum desmode des_mode;	/* mode */
	u_char des_ivec[8];	/* input vector */
	unsigned des_len;	/* number of bytes to crypt */
	union {
		u_char UDES_data[DES_QUICKLEN];
		u_char *UDES_buf;
	} UDES;
#define	des_data	UDES.UDES_data	/* direct data here if quick */
#define	des_buf		UDES.UDES_buf	/* otherwise, pointer to data */
};

/*
 * Encrypt an arbitrary sized buffer
 */
#define	DESIOCBLOCK	_IOWR('d', 6, struct desparams)

/*
 * Encrypt of small amount of data, quickly
 */
#define	DESIOCQUICK	_IOWR('d', 7, struct desparams)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DES_H */
