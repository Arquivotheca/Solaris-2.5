/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _EUC_H
#define	_EUC_H

#pragma ident	"@(#)euc.h	1.6	93/04/15 SMI"

#include <sys/euc.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__STDC__
extern int csetcol(int n);	/* Returns # of columns for codeset n. */
extern int csetlen(int n);	/* Returns # of bytes excluding SSx. */
#else	/* __STDC__ */
extern int csetlen(), csetcol();
#endif	/* __STDC__ */

/* Returns code set number for the first byte of an EUC char. */
#define	csetno(c) \
	(((c)&0x80)?(((c)&0xff) == SS2)?2:((((c)&0xff) == SS3)?3:1):0)

/*
 * Copied from _wchar.h of SVR4
 */
#define	_ctype		__ctype
#define	multibyte	(_ctype[520] > 1)
#define	eucw1		_ctype[514]
#define	eucw2		_ctype[515]
#define	eucw3		_ctype[516]
#define	scrw1	_ctype[517]
#define	scrw2	_ctype[518]
#define	scrw3	_ctype[519]

#ifdef	__cplusplus
}
#endif

#endif	/* _EUC_H */
