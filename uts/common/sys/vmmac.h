/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
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
 *
 */

#ifndef _SYS_VMMAC_H
#define	_SYS_VMMAC_H

#pragma ident	"@(#)vmmac.h	2.30	92/07/14 SMI"	/* SVr4.0 1.7 */

#include <sys/sysmacros.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Turn virtual addresses into kernel map indices.
 *
 * "Sysmap" is an array of page table entries used to map virtual
 * addresses, starting at (kernel virtual address) Sysbase, to many
 * different things.  Sysmap is managed throught the resource map named
 * "kernelmap".  kmx means kernelmap index, the index (into Sysmap)
 * returned by rmalloc(kernelmap, ...).
 *
 * kmxtob expects an (integer) kernel map index and returns the virtual
 * address by the mmu page number.  btokmx expects a (caddr_t) virtual
 * address and returns the integer kernel map index.
 */
#define	kmxtob(a)	(Sysbase + ((a) << MMU_PAGESHIFT))
#define	btokmx(b)	(((caddr_t)(b) - Sysbase) >> MMU_PAGESHIFT)

/*
 * Ethernet addressable kernel map.
 */
#define	ekmxtob(a)	(E_Sysbase + ((a) << MMU_PAGESHIFT))
#define	btoekmx(b)	(((caddr_t)(b) - E_Sysbase) >> MMU_PAGESHIFT)

/* Average new into old with aging factor time */
#define	ave(smooth, cnt, time) \
	smooth = ((time - 1) * (smooth) + (cnt)) / (time)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VMMAC_H */
