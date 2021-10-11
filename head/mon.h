/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _MON_H
#define	_MON_H

#pragma ident	"@(#)mon.h	1.9	95/08/28 SMI"	/* SVr4.0 1.8.2.2 */

#ifdef	__cplusplus
extern "C" {
#endif

struct hdr {
	char	*lpc;
	char	*hpc;
	int	nfns;
};

struct cnt {
	char	*fnpc;
	long	mcnt;
};

typedef unsigned short WORD;

#define	MON_OUT	"mon.out"
#define	MPROGS0	(150 * sizeof (WORD))	/* 300 for pdp11, 600 for 32-bits */
#define	MSCALE0	4
#ifndef NULL
#define	NULL	0
#endif

#if defined(__STDC__)
extern void monitor(int (*)(), int (*)(), WORD *, int, int);
#else
extern void monitor();
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _MON_H */
