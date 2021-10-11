/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

#ifndef _MP_H
#define	_MP_H

#pragma ident	"@(#)mp.h	1.7	92/07/14 SMI"	/* SVr4.0 1.2	*/

#ifdef	__cplusplus
extern "C" {
#endif

struct mint {
	int len;
	short *val;
};
typedef struct mint MINT;


#ifdef __STDC__
extern MINT *itom(int);
extern MINT *xtom(char *);
extern char *mtox(MINT *);
extern short *xalloc(int, char *);
extern void mfree(MINT *);
#else
extern MINT *itom();
extern MINT *xtom();
extern char *mtox();
extern short *xalloc();
extern void mfree();
#endif

#define	FREE(x)	xfree(&(x))		/* Compatibility */

#ifdef	__cplusplus
}
#endif

#endif /* _MP_H */
