/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_BITMAP_H
#define	_SYS_BITMAP_H

#pragma ident	"@(#)bitmap.h	1.10	93/04/05 SMI"	/* SVr4.0 1.6	*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Operations on bitmaps of arbitrary size
 * A bitmap is a vector of 1 or more ulongs.
 * The user of the package is responsible for range checks and keeping
 * track of sizes.
 */

/*
 * REQUIRES sys/types.h
 */

#define	BT_NBIPUL	32	/* n bits per ulong */
#define	BT_ULSHIFT	5	/* log base 2 of BT_NBIPUL, */
				/* to extract word index */
#define	BT_ULMASK	0x1f	/* to extract bit index */

/*
 * bitmap is a ulong *, bitindex an index_t
 *
 * The macros BT_WIM and BT_BIW internal; there is no need
 * for users of this package to use them.
 */

/*
 * word in map
 */
#define	BT_WIM(bitmap, bitindex) \
	((bitmap)[(bitindex) >> BT_ULSHIFT])
/*
 * bit in word
 */
#define	BT_BIW(bitindex) \
	(1 << ((bitindex) & BT_ULMASK))

/*
 * These are public macros
 *
 * BT_BITOUL == n bits to n ulongs
 */
#define	BT_BITOUL(nbits) \
	(((nbits) + BT_NBIPUL -1) / BT_NBIPUL)
#define	BT_TEST(bitmap, bitindex) \
	((BT_WIM((bitmap), (bitindex)) & BT_BIW(bitindex)) ? 1 : 0)
#define	BT_SET(bitmap, bitindex) \
	{ BT_WIM((bitmap), (bitindex)) |= BT_BIW(bitindex); }
#define	BT_CLEAR(bitmap, bitindex) \
	{ BT_WIM((bitmap), (bitindex)) &= ~BT_BIW(bitindex); }


#if defined(__STDC__)
/*
 * return next available bit index from map with specified number of bits
 */
extern index_t	bt_availbit(ulong *bitmap, size_t nbits);
/*
 * find the highest order bit that is on, and is within or below
 * the word specified by wx
 */
extern void	bt_gethighbit(ulong *mapp, int wx, int *bitposp);
extern int	bt_range(ulong *bitmap, size_t *pos1, size_t *pos2,
			size_t nbits);
/*
 * Find highest and lowest one bit set.
 *	Returns bit number + 1 of bit that is set, otherwise returns 0.
 * Low order bit is 0, high order bit is 31.
 */
extern int	highbit(ulong);
extern int	lowbit(ulong);
#else
extern index_t	bt_availbit();
extern void	bt_gethighbit();
extern int	bt_range();
extern int	highbit();
extern int	lowbit();
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BITMAP_H */
