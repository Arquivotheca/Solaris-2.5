/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ident	"@(#)bitmap.c	1.13	94/09/16 SMI"	/* from SVR4.0 1.4 */

/*
 * Operations on bitmaps of arbitrary size
 * A bitmap is a vector of 1 or more ulongs.
 * The user of the package is responsible for range checks and keeping
 * track of sizes.
 */

#include <sys/types.h>
#include <sys/bitmap.h>

int	highbit(ulong i);		/* find highest bit set */

/*
 * Return index of first available bit in denoted bitmap, or -1 for
 * failure.  Size is the cardinality of the bitmap; that is, the
 * number of bits.
 * No side-effects.  In particular, does not update bitmap.
 * Caller is responsible for range checks.
 */
index_t
bt_availbit(bitmap, nbits)
	register ulong		*bitmap;
	size_t			nbits;
{
	register index_t	maxword;	/* index of last in map */
	register index_t	wx;		/* word index in map */

	/*
	 * Look for a word with a bit off.
	 * Subtract one from nbits because we're converting it to a
	 * a range of indices.
	 */
	nbits -= 1;
	maxword = nbits >> BT_ULSHIFT;
	for (wx = 0; wx <= maxword; wx++) {
		if (bitmap[wx] != ~0) {
			break;
		}
	}
	if (wx <= maxword) {
		/*
		 * Found a word with a bit off.  Now find the bit in the word.
		 */
		register index_t	bx;	/* bit index in word */
		register index_t	maxbit; /* last bit to look at */
		register ulong		word;
		register ulong		bit;

		maxbit = wx == maxword ? nbits & BT_ULMASK : BT_NBIPUL - 1;
		word = bitmap[wx];
		bit = 1;
		for (bx = 0; bx <= maxbit; bx++, bit <<= 1) {
			if (!(word & bit)) {
				return (wx << BT_ULSHIFT | bx);
			}
		}
	}
	return (-1);
}


/*
 * Find highest order bit that is on, and is within or below
 * the word specified by wx.
 */
void
bt_gethighbit(mapp, wx, bitposp)
	register ulong	*mapp;
	register int	wx;
	int		*bitposp;
{
	register ulong	word;

	while ((word = mapp[wx]) == 0) {
		wx--;
		if (wx < 0) {
			*bitposp = -1;
			return;
		}
	}
	*bitposp = wx << BT_ULSHIFT | (highbit(word) - 1);
}


/*
 * Search the bitmap for a consecutive pattern of 1's.
 * Search starts at position pos1.
 * Returns 1 on success and 0 on failure.
 * Side effects.
 * Returns indices to the first bit (pos1)
 * and the last bit (pos2) in the pattern.
 */
int
bt_range(bitmap, pos1, pos2, nbits)
	register ulong *bitmap;
	register size_t *pos1;
	register size_t *pos2;
	size_t	nbits;
{
	register size_t inx;

	for (inx = *pos1; inx < nbits; inx++)
		if (BT_TEST(bitmap, inx))
			break;

	if (inx == nbits)
		return (0);

	*pos1 = inx;

	for (; inx < nbits; inx++)
		if (!BT_TEST(bitmap, inx))
			break;

	*pos2 = inx - 1;

	return (1);
}

/*
 * Find highest one bit set.
 *	Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * High order bit is 31.
 */
int
highbit(ulong i)
{
	register int h = 1;

	if (i == 0)
		return (i);

	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}

/*
 * Find lowest one bit set.
 *	Returns bit number + 1 of lowest bit that is set, otherwise returns 0.
 * Low order bit is 0.
 */
int
lowbit(ulong i)
{
	register int h = 1;

	if (i == 0)
		return (i);

	if (!(i & 0xffff)) {
		h += 16; i >>= 16;
	}
	if (!(i & 0xff)) {
		h += 8; i >>= 8;
	}
	if (!(i & 0xf)) {
		h += 4; i >>= 4;
	}
	if (!(i & 0x3)) {
		h += 2; i >>= 2;
	}
	if (!(i & 0x1)) {
		h += 1;
	}
	return (h);
}
