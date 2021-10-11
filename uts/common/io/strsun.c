#ident	"@(#)strsun.c	1.11	95/02/01 SMI"

/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

/*
 *  SunOS common STREAMS utility routines.
 *
 *  Refer to:
 *    Neal Nuckolls, "SunOS Datalink Architecture",
 *    Sun Microsystems, xx/yy/zz.
 */

#include	<sys/types.h>
#include	<sys/time.h>
#include	<sys/systm.h>
#include	<sys/errno.h>
#include	<sys/stream.h>
#include	<sys/sysmacros.h>
#include	<sys/strsun.h>

void
merror(wq, mp, error)
queue_t	*wq;
mblk_t	*mp;
int	error;
{
	if ((mp == mexchange(wq, mp, 1, M_ERROR, -1)) == NULL)
		return;
	*mp->b_rptr = (unsigned char) error;
	qreply(wq, mp);
}

/*
 * Convert an M_IOCTL into an M_IOCACK.
 * Assumption:  mp points to an M_IOCTL msg.
 */
void
miocack(wq, mp, count, error)
queue_t	*wq;
mblk_t	*mp;
int	count;
int	error;
{
	struct	iocblk	*iocp;

	mp->b_datap->db_type = M_IOCACK;
	iocp = (struct iocblk *) mp->b_rptr;
	iocp->ioc_count = count;
	iocp->ioc_error = error;
	qreply(wq, mp);
}

/*
 * Convert an M_IOCTL into an M_IOCNAK.
 * Assumption:  mp points to an M_IOCTL msg.
 */
void
miocnak(wq, mp, count, error)
queue_t	*wq;
mblk_t	*mp;
int	count;
int	error;
{
	struct	iocblk	*iocp;

	mp->b_datap->db_type = M_IOCNAK;
	iocp = (struct iocblk *) mp->b_rptr;
	iocp->ioc_count = count;
	iocp->ioc_error = error;
	qreply(wq, mp);
}

/*
 * Exchange one msg for another.  Free old msg and allocate
 * a new one if either (1) mp is NULL, (2), requested size
 * is larger than current size, or (3) reference count of
 * the current msg is greater than one.
 * Set db_type and b_rptr/b_wptr appropriately.
 * Set the first longword of the msg to 'primtype' if
 * 'primtype' is not -1.
 *
 * On allocb() failure, return NULL after sending an
 * M_ERROR msg w/ENOSR error upstream.
 */
mblk_t *
mexchange(wq, mp, size, type, primtype)
queue_t	*wq;
mblk_t	*mp;
int	size;
int	type;
long	primtype;
{
	if ((mp == NULL) || (MBLKSIZE(mp) < size) || (DB_REF(mp) > 1)) {
		freemsg(mp);
		if ((mp = allocb(size, BPRI_LO)) == NULL) {
			if (mp = allocb(1, BPRI_HI))
				merror(wq, mp, ENOSR);
			return (NULL);
		}
	}
	mp->b_datap->db_type = (short)type;
	mp->b_rptr = mp->b_datap->db_base;
	mp->b_wptr = mp->b_rptr + size;
	if (primtype >= 0)
		*(long *)mp->b_rptr = primtype;
	return (mp);
}

/*
 * Just count the stupid bytes and, no, I don't care what
 * the bleedin' mblk types are.
 */
int
msgsize(mp)
register	mblk_t	*mp;
{
	register	int	n = 0;

	for (; mp; mp = mp->b_cont)
		n += MBLKL(mp);

	return (n);
}

/*
 * Expand the data buffer free head and tail sizes to minhead and mintail
 * by allocating a new, larger mblk, copying the original data into this
 * new one, and tossing the old one.  The mblk's after the first are
 * preserved.  The new message is returned.
 */
mblk_t *
mexpandb(mp, minhead, mintail)
mblk_t	*mp;
int	minhead, mintail;
{
	int	size;
	int	len;
	mblk_t	*tmp, *contp;

	len = MBLKL(mp);
	size = minhead + len + mintail;

	contp = unlinkb(mp);
	if ((tmp = allocb(size, BPRI_LO)) == NULL) {
		freemsg(contp);
		return (NULL);
	}
	if (contp)
		linkb(tmp, contp);

	tmp->b_rptr += minhead;
	bcopy((caddr_t)mp->b_rptr, (caddr_t)tmp->b_rptr, len);
	tmp->b_wptr = tmp->b_rptr + len;

	freeb(mp);

	return (tmp);
}

/*
 * Copy data from msg to buffer and free the msg.
 */
void
mcopymsg(mp, bufp)
mblk_t	*mp;
u_char	*bufp;
{
	mblk_t	*bp;
	int	n;

	for (bp = mp; bp; bp = bp->b_cont) {
		n = MBLKL(bp);
		bcopy((caddr_t)bp->b_rptr, (caddr_t)bufp, n);
		bufp += n;
	}

	freemsg(mp);
}
