#pragma ident	"@(#)ip_cksum.c	1.10	94/05/17 SMI"

/*
 * Copyright (c) 1983 - 1992 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/debug.h>
#include <sys/ddi.h>
#include <sys/vtrace.h>

extern unsigned int 	ip_ocsum(u_short *address, int halfword_count, 
				 unsigned int sum);
extern unsigned int	ip_ocsum_copy(u_short *address, int halfword_count, 
				      unsigned int sum, u_short *dest);

/*
 * Checksum routine for Internet Protocol family headers.
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 */

#define mp_len(mp) ((mp)->b_wptr - (mp)->b_rptr)

/*
 * Note: this does not ones-complement the result since it is used
 * when computing partial checksums.
 * For nonSTRUIO_IP mblks, assumes mp->b_rptr+offset is 16 bit alligned.
 * For STRUIO_IP mblks, assumes mp->b_datap->db_struiobase is 16 bit alligned.
 *
 * Note: for STRUIO_IP special mblks some data may have been previously
 *	 checksumed, this routine will handle additional data prefixed
 *	 within an mblk or b_cont (chained) mblk(s). This routine will
 *	 also handle suffixed b_cont mblk(s) but not data suffixed within
 *	 an mblk !!!
 */
unsigned int
ip_cksum(mp, offset, sum)
	register mblk_t *mp;
	register int offset;
	register unsigned int sum;
{
	register u_short *w;
	register int mlen;
	register int pmlen;
	register mblk_t *pmp;
	register dblk_t *dp = mp->b_datap;
	register u_short psum = 0;

	ASSERT(dp);
	TRACE_4(TR_FAC_IP, TR_IP_CKSUM_START,
	    "ip_cksum_start:%X size %d off %d (%X)",
	    mp, msgdsize(mp), offset, sum);

	if (mp->b_cont == NULL) {
		/*
		 * May be fast-path, only one mblk.
		 */
		w = (u_short *)(mp->b_rptr + offset);
		if (dp->db_struioflag & STRUIO_IP) {
			/*
			 * Checksum any data not already done by
			 * the caller and add in any partial checksum.
			 */
			if ((u_char *)w > dp->db_struiobase
			  || mp->b_wptr < dp->db_struiolim)
				/*
				 * Mblk data pointers aren't inclusive
				 * of uio data, so disregard checksum.
				 */
				goto norm;
			ASSERT(mp->b_wptr == dp->db_struiolim);
			psum = *(u_short *)dp->db_struioun.data;
			if ((mlen = dp->db_struiobase - (u_char *)w) < 0)
				mlen = 0;
			if (mlen & 1)
				goto slow;
			if (mlen && dp->db_struiobase != dp->db_struioptr
			  && dp->db_struiolim != dp->db_struioptr) {
				/*
				 * There is prefix data to do and some uio
				 * data has already been checksumed and there
				 * is more uio data to do, so do the prefix
				 * data first, then do the remainder of the
				 * uio data. 
				 */
				sum = ip_ocsum(w, mlen >> 1, sum);
				w = (u_short *)dp->db_struioptr;
				if ((int)w & 1) {
					pmp = mp;
					goto slow1;
				}
				mlen = dp->db_struiolim - dp->db_struioptr;
			} else if (dp->db_struiolim != dp->db_struioptr) {
				/*
				 * There may be uio data to do, if there is
				 * prefix data to do then add in all of the
				 * uio data (if any) to do, else just do any
				 * uio data.
				 */
				if (mlen)
					mlen += dp->db_struiolim
						- dp->db_struioptr;
				else {
					w = (u_short *)dp->db_struioptr;
					if ((int)w & 1)
						goto slow;
					mlen = dp->db_struiolim
						- dp->db_struioptr;
				}
			} else if (mlen == 0)
#ifdef TRACE
	{
		TRACE_4(TR_FAC_IP, TR_IP_CKSUM_START,
		    "ip_cksum_start:%X size %d off %d (%X)",
		    mp, mlen, dp->db_struioflag, sum);
		sum += psum;
		TRACE_3(TR_FAC_IP, TR_IP_CKSUM_END,
			"ip_cksum_end:(%S) type %d (%X)", "ip_cksum", 2, sum);
		return (sum);
	}
#else
				return (psum);
#endif
			if (mlen & 1)
				goto slow;
			sum += psum;
		} else {
			/*
			 * Checksum all data not already done by the caller.
			 */
		norm:
			mlen = mp->b_wptr - (u_char *)w;
			if (mlen & 1)
				goto slow;
		}
		ASSERT(((int)w & 1) == 0);
		ASSERT((mlen & 1) == 0);
#ifdef TRACE
		TRACE_4(TR_FAC_IP, TR_IP_CKSUM_START,
		    "ip_cksum_start:%X size %d off %d (%X)",
		    mp, mlen, dp->db_struioflag, sum);
		sum = ip_ocsum(w, mlen >> 1, sum);
		TRACE_3(TR_FAC_IP, TR_IP_CKSUM_END,
			"ip_cksum_end:(%S) type %d (%X)", "ip_cksum", 0, sum);
		return (sum);
#else
		return (ip_ocsum(w, mlen >> 1, sum));
#endif
	}
	if (dp->db_struioflag & STRUIO_IP)
		psum = *(u_short *)dp->db_struioun.data;
slow:
	pmp = 0;
slow1:
	mlen = 0;
	pmlen = 0;
	for (; ;) {
		/*
		 * Each trip around loop adds in word(s) from one mbuf segment
		 * (except for when pmp == mp, then its two partial trips).
		 */
		w = (u_short *)(mp->b_rptr + offset);
		if (pmp) {
			/*
			 * This is the second trip around for this mblk.
			 */
			pmp = 0;
			mlen = 0;
			goto douio;
		} else if (dp->db_struioflag & STRUIO_IP) {
			/*
			 * Checksum any data not already done by the
			 * caller and add in any partial checksum.
			 */
			if ((u_char *)w > dp->db_struiobase
			  || mp->b_wptr < dp->db_struiolim)
				/*
				 * Mblk data pointers aren't inclusive
				 * of uio data, so disregard checksum.
				 */
				goto snorm;
			ASSERT(mp->b_wptr == dp->db_struiolim);
			if ((mlen = dp->db_struiobase - (u_char *)w) < 0)
				mlen = 0;
			if (mlen && dp->db_struiobase != dp->db_struioptr) {
				/*
				 * There is prefix data too do and some
				 * uio data has already been checksumed,
				 * so do the prefix data only this trip.
				 */
				pmp = mp;
			} else {
				/*
				 * Add in any partial cksum (if any) and
				 * do the remainder of the uio data.
				 */
				int odd;
			douio:
				odd = (dp->db_struioptr - dp->db_struiobase)&1;
				if (pmlen == -1) {
					/*
					 * Previous mlen was odd, so swap
					 * the partial checksum bytes.
					 */
					sum += (psum << 8) & 0xffff
					     | (psum >> 8);
					if (odd)
						pmlen = 0;
				} else {
					sum += psum;
					if (odd)
						pmlen = -1;
				}
				if (dp->db_struiolim != dp->db_struioptr) {
					/*
					 * If prefix data to do and then all
					 * the uio data nees to be checksumed,
					 * else just do any uio data.
					 */
					if (mlen)
						mlen += dp->db_struiolim
							- dp->db_struioptr;
					else {
						w = (u_short *)dp->db_struioptr;
						mlen = dp->db_struiolim
							- dp->db_struioptr;
					}
				}
			}
		} else {
			/*
			 * Checksum all of the mblk data.
			 */
		snorm:
			mlen = mp->b_wptr - (u_char *)w;
		}
#ifdef	TRACE
		TRACE_4(TR_FAC_IP, TR_IP_CKSUM_START,
		    "ip_cksum_start:%X size %d off %d (%X)",
		    mp, mlen, dp->db_struioflag, sum)
#endif
		mp = mp->b_cont;
		if (mlen > 0 && pmlen == -1) {
			/*
			 * There is a byte left from the last
			 * segment; add it into the checksum.
			 * Don't have to worry about a carry-
			 * out here because we make sure that
			 * high part of (32 bit) sum is small
			 * below.
			 */
#ifdef _LITTLE_ENDIAN
			sum += *(u_char *)w << 8;
#else
			sum += *(u_char *)w;
#endif
			w = (u_short *)((char *)w + 1);
			mlen--;
			pmlen = 0;
		}
		if (mlen > 0) {
			if (((int)w & 1) == 0) {
				sum = ip_ocsum(w, mlen>>1, sum);
				w += mlen>>1;
				/*
				 * If we had an odd number of bytes,
				 * then the last byte goes in the high
				 * part of the sum, and we take the
				 * first byte to the low part of the sum
				 * the next time around the loop.
				 */
				if (mlen & 1) {
#ifdef _LITTLE_ENDIAN
					sum += *(u_char *)w;
#else
					sum += *(u_char *)w << 8;
#endif
					pmlen = -1;
				}
			} else {
				u_short swsum;
#ifdef _LITTLE_ENDIAN
				sum += *(u_char *)w;
#else
				sum += *(u_char *)w << 8;
#endif
				mlen--;
				w = (u_short *)(1 + (int)w);

				/* Do a separate checksum and copy operation */
				swsum = ip_ocsum(w, mlen>>1, 0);
				sum += (swsum << 8) & 0xffff | (swsum >> 8);
				w += mlen>>1;
				/*
				 * If we had an even number of bytes,
				 * then the last byte goes in the low
				 * part of the sum.  Otherwise we had an
				 * odd number of bytes and we take the first
				 * byte to the low part of the sum the
				 * next time around the loop.
				 */
				if (mlen & 1) {
#ifdef _LITTLE_ENDIAN
					sum += *(u_char *)w << 8;
#else
					sum += *(u_char *)w;
#endif
				}
				else
					pmlen = -1;
			}
		}
		/*
		 * Locate the next block with some data.
		 * If there is a word split across a boundary we
		 * will wrap to the top with mlen == -1 and
		 * then add it in shifted appropriately.
		 */
		offset = 0;
		if (! pmp) {
			for (; ;) {
				if (mp == 0) {
					goto done;
				}
				if (mp_len(mp))
					break;
				mp = mp->b_cont;
			}
			dp = mp->b_datap;
			if (dp->db_struioflag & STRUIO_IP)
				psum = *(u_short *)dp->db_struioun.data;
		} else
			mp = pmp;
	}
done:
	/*
	 * Add together high and low parts of sum
	 * and carry to get cksum.
	 * Have to be careful to not drop the last
	 * carry here.
	 */
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	TRACE_3(TR_FAC_IP, TR_IP_CKSUM_END,
		"ip_cksum_end:(%S) type %d (%X)", "ip_cksum", 1, sum);
	return (sum);
}

unsigned int
ip_old_cksum(mp, offset, sum)
	register mblk_t *mp;
	register int offset;
	register unsigned int sum;
{
	register u_short *w;
	register int mlen;

	ASSERT(((int)(mp->b_rptr + offset) & 1) == 0);
	TRACE_1(TR_FAC_IP, TR_IP_CKSUM_START,
			"ip_cksum_start:size %d", msgdsize(mp));
	if (mp->b_cont == NULL && ((int)mp->b_wptr & 1) == 0) {
		w = (u_short *)(mp->b_rptr + offset);
#ifdef TRACE
		sum = ip_ocsum(w, (mp->b_wptr - (u_char *)w) >> 1, sum);
		TRACE_2(TR_FAC_IP, TR_IP_CKSUM_END,
			"ip_cksum_end:(%S) type %d", "ip_ocsum", 0);
		return(sum);
#else
		return (ip_ocsum(w, (mp->b_wptr - (u_char *)w) >> 1, sum));
#endif
	}
	mlen = 0;
	for (; ;) {
		/*
		 * Each trip around loop adds in
		 * word from one mbuf segment.
		 */
		w = (u_short *)(mp->b_rptr + offset);
		if (mlen == -1) {
			/*
			 * There is a byte left from the last segment;
			 * add it into the checksum.  Don't have to worry
			 * about a carry-out here because we make sure
			 * that high part of (32 bit) sum is small below.
			 */
#ifdef _LITTLE_ENDIAN
			sum += *(u_char *)w << 8;
#else
			sum += *(u_char *)w;
#endif
			w = (u_short *)((char *)w + 1);
		}
		mlen = mp->b_wptr - (u_char *)w;
		mp = mp->b_cont;
		if (mlen > 0) {
			if (((int)w & 1) == 0) {
				sum = ip_ocsum(w, mlen>>1, sum);
				w += mlen>>1;
				/*
				 * If we had an odd number of bytes,
				 * then the last byte goes in the high
				 * part of the sum, and we take the
				 * first byte to the low part of the sum
				 * the next time around the loop.
				 */
				if (mlen & 1) {
#ifdef _LITTLE_ENDIAN
					sum += *(u_char *)w;
#else
					sum += *(u_char *)w << 8;
#endif
					mlen = -1;
				}
			} else {
				u_short swsum;

#ifdef _LITTLE_ENDIAN
				sum += *(u_char *)w;
#else
				sum += *(u_char *)w << 8;
#endif
				mlen--;
				w = (u_short *)(1 + (int)w);

				/* Do a separate checksum and copy operation */
				swsum = ip_ocsum(w, mlen>>1, 0);
				sum += (swsum << 8) & 0xffff | (swsum >> 8);
				w += mlen>>1;
				/*
				 * If we had an even number of bytes,
				 * then the last byte goes in the low
				 * part of the sum.  Otherwise we had an
				 * odd number of bytes and we take the first
				 * byte to the low part of the sum the
				 * next time around the loop.
				 */
				if (mlen & 1) {
#ifdef _LITTLE_ENDIAN
					sum += *(u_char *)w << 8;
#else
					sum += *(u_char *)w;
#endif
				}
				else
					mlen = -1;
			}
		}
		/*
		 * Locate the next block with some data.
		 * If there is a word split across a boundary we
		 * will wrap to the top with mlen == -1 and
		 * then add it in shifted appropriately.
		 */
		offset = 0;
		for (; ;) {
			if (mp == 0) {
				goto done;
			}
			if (mp_len(mp))
				break;
			mp = mp->b_cont;
		}
	}
done:
	/*
	 * Add together high and low parts of sum
	 * and carry to get cksum.
	 * Have to be careful to not drop the last
	 * carry here.
	 */
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	TRACE_2(TR_FAC_IP, TR_IP_CKSUM_END,
		"ip_cksum_end:(%S) type %d", "ip_cksum", 1);
	return (sum);
}

/*
 * Note: this does not ones-complement the result since it is used
 * when computing partial checksums.
 * Assumes mp->b_rptr+offset is 16 bit alligned.
 */
unsigned int
ip_cksum_copy(mp, offset, sum, dest)
	register mblk_t *mp;
	register int offset;
	register unsigned int sum;
	u_char	*dest;
{
	register u_short *w;
	register int mlen;

	ASSERT(((int)(mp->b_rptr + offset) & 1) == 0);
	TRACE_1(TR_FAC_IP, TR_IP_CKSUM_COPY_START,
			"ip_cksum_copy_start:size %d", msgdsize(mp));
	if (mp->b_cont == NULL && ((int)mp->b_wptr & 1) == 0) {
		w = (u_short *)(mp->b_rptr + offset);
		if (((int)dest & 0x7) == ((int)w  & 0x7)) {
#ifdef TRACE
			sum = ip_ocsum_copy(w, 
					      (mp->b_wptr - (u_char *)w) >> 1, 
					      sum, (u_short *)dest);
			TRACE_2(TR_FAC_IP, TR_IP_CKSUM_COPY_END,
				"ip_cksum_copy_end:(%S) type %d", "ip_ocsum_copy", 0);
			return(sum);
#else
			return (ip_ocsum_copy(w, 
					      (mp->b_wptr - (u_char *)w) >> 1, 
					      sum, (u_short *)dest));
#endif
		} else {
			mlen = mp->b_wptr - (u_char *)w;
			bcopy((char *)w, (char *)dest, (unsigned)mlen);
#ifdef TRACE
			sum = ip_ocsum(w, mlen >> 1, sum);
			TRACE_2(TR_FAC_IP, TR_IP_CKSUM_COPY_END,
				"ip_cksum_copy_end:(%S) type %d", "ip_ocsum", 1);
			return(sum);
#else
			return (ip_ocsum(w, mlen >> 1, sum));
#endif
		}
	}
	mlen = 0;
	for (; ;) {
		/*
		 * Each trip around loop adds in
		 * word from one mbuf segment.
		 */
		w = (u_short *)(mp->b_rptr + offset);
		if (mlen == -1) {
			/*
			 * There is a byte left from the last segment;
			 * add it into the checksum.  Don't have to worry
			 * about a carry-out here because we make sure
			 * that high part of (32 bit) sum is small below.
			 */
#ifdef _LITTLE_ENDIAN
			sum += *(u_char *)w << 8;
#else
			sum += *(u_char *)w;
#endif
			*dest++ = *(u_char *)w;
			w = (u_short *)((char *)w + 1);
		}
		mlen = mp->b_wptr - (u_char *)w;
		mp = mp->b_cont;
		if (mlen > 0) {
			if (((int)w & 1) == 0) {
				if (((int)dest & 0x7) == ((int)w  & 0x7))
					sum = ip_ocsum_copy(w, mlen>>1, 
							    sum, 
							    (u_short *)dest);
				else {
					bcopy((char *)w, (char *)dest, 
					      (unsigned)mlen);
					sum = ip_ocsum(w, mlen>>1, sum);
				}
				w += mlen>>1;
				dest += mlen & ~1;
				/*
				 * If we had an odd number of bytes,
				 * then the last byte goes in the high
				 * part of the sum, and we take the
				 * first byte to the low part of the sum
				 * the next time around the loop.
				 */
				if (mlen & 1) {
#ifdef _LITTLE_ENDIAN
					sum += *(u_char *)w;
#else
					sum += *(u_char *)w << 8;
#endif
					*dest++ = *(u_char *)w;
					mlen = -1;
				}
			} else {
				u_short swsum;

#ifdef _LITTLE_ENDIAN
				sum += *(u_char *)w;
#else
				sum += *(u_char *)w << 8;
#endif
				*dest++ = *(u_char *)w;
				mlen--;
				w = (u_short *)(1 + (int)w);

				/* Do a separate checksum and copy operation */
				bcopy((char *)w, (char *)dest, (unsigned)mlen);
				swsum = ip_ocsum(w, mlen>>1, 0);
				sum += (swsum << 8) & 0xffff | (swsum >> 8);
				w += mlen>>1;
				dest += mlen & ~1;
				/*
				 * If we had an even number of bytes,
				 * then the last byte goes in the low
				 * part of the sum.  Otherwise we had an
				 * odd number of bytes and we take the first
				 * byte to the low part of the sum the
				 * next time around the loop.
				 */
				if (mlen & 1) {
#ifdef _LITTLE_ENDIAN
					sum += *(u_char *)w << 8;
#else
					sum += *(u_char *)w;
#endif
					*dest++ = *(u_char *)w;
				}
				else
					mlen = -1;
			}
		}
		/*
		 * Locate the next block with some data.
		 * If there is a word split across a boundary we
		 * will wrap to the top with mlen == -1 and
		 * then add it in shifted appropriately.
		 */
		offset = 0;
		for (; ;) {
			if (mp == 0) {
				goto done;
			}
			if (mp_len(mp))
				break;
			mp = mp->b_cont;
		}
	}
done:
	/*
	 * Add together high and low parts of sum
	 * and carry to get cksum.
	 * Have to be careful to not drop the last
	 * carry here.
	 */
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	TRACE_2(TR_FAC_IP, TR_IP_CKSUM_COPY_END,
		"ip_cksum_copy_end:(%S) type %d", "ip_cksum_copy", 2);
	return (sum);
}
