/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_FBUF_H
#define	_SYS_FBUF_H

#pragma ident	"@(#)fbuf.h	1.11	92/07/14 SMI"	/* SVr4.0 1.3	*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A struct fbuf is used to get a mapping to part of a file using the
 * segkmap facilities.  After you get a mapping, you can fbrelse() it
 * (giving a seg code to pass back to segmap_release), you can fbwrite()
 * it (causes a synchronous write back using the file mapping information),
 * or you can fbiwrite it (causing indirect synchronous write back to
 * the block number given without using the file mapping information).
 */

struct fbuf {
	caddr_t	fb_addr;
	u_int	fb_count;
};

extern int fbread(/* vp, off, len, rw, fbpp */);
extern void fbzero(/* vp, off, len, fbpp */);
extern int fbwrite(/* fbp */);
extern int fbdwrite(/* fbp */);
extern int fbiwrite(/* fbp, vp, bn, bsize */);
extern void fbrelse(/* fbp, rw */);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FBUF_H */
