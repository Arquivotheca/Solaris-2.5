#ident	"@(#)sundlpi.c	1.6	93/07/21 SMI"

/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

/*
 *  Common Sun DLPI routines.
 */

#include	<sys/types.h>
#include	<sys/sysmacros.h>
#include	<sys/systm.h>
#include	<sys/stream.h>
#include	<sys/strsun.h>
#include	<sys/dlpi.h>

#define		DLADDRL		(80)

void
dlbindack(wq, mp, sap, addrp, addrlen, maxconind, xidtest)
queue_t	*wq;
mblk_t	*mp;
u_long	sap;
u_char	*addrp;
int	addrlen;
int	maxconind;
u_long	xidtest;
{
	register union DL_primitives	*dlp;
	int size;

	size = sizeof (dl_bind_ack_t) + addrlen;
	if ((mp = mexchange(wq, mp, size, M_PCPROTO, DL_BIND_ACK))
		== NULL)
		return;

	dlp = (union DL_primitives *) mp->b_rptr;
	dlp->bind_ack.dl_sap = sap;
	dlp->bind_ack.dl_addr_length = addrlen;
	dlp->bind_ack.dl_addr_offset = sizeof (dl_bind_ack_t);
	dlp->bind_ack.dl_max_conind = maxconind;
	dlp->bind_ack.dl_xidtest_flg = xidtest;
	bcopy((caddr_t)addrp, (caddr_t)(mp->b_rptr + sizeof (dl_bind_ack_t)),
		addrlen);

	qreply(wq, mp);
}

void
dlokack(wq, mp, correct_primitive)
queue_t	*wq;
mblk_t	*mp;
u_long	correct_primitive;
{
	register	union	DL_primitives	*dlp;

	if ((mp = mexchange(wq, mp, sizeof (dl_ok_ack_t), M_PCPROTO,
			DL_OK_ACK)) == NULL)
		return;
	dlp = (union DL_primitives *) mp->b_rptr;
	dlp->ok_ack.dl_correct_primitive = correct_primitive;
	qreply(wq, mp);
}

void
dlerrorack(wq, mp, error_primitive, errno, unix_errno)
queue_t	*wq;
mblk_t	*mp;
u_long	error_primitive;
u_long	errno;
u_long	unix_errno;
{
	register	union	DL_primitives	*dlp;

	if ((mp = mexchange(wq, mp, sizeof (dl_error_ack_t), M_PCPROTO,
		DL_ERROR_ACK)) == NULL)
		return;
	dlp = (union DL_primitives *) mp->b_rptr;
	dlp->error_ack.dl_error_primitive = error_primitive;
	dlp->error_ack.dl_errno = errno;
	dlp->error_ack.dl_unix_errno = unix_errno;
	qreply(wq, mp);
}

void
dluderrorind(wq, mp, addrp, addrlen, errno, unix_errno)
queue_t	*wq;
mblk_t	*mp;
u_char	*addrp;
u_long	addrlen;
u_long	errno;
{
	register	union	DL_primitives	*dlp;
	char	buf[DLADDRL];
	int	size;

	if (addrlen > DLADDRL)
		addrlen = DLADDRL;

	bcopy((caddr_t) addrp, (caddr_t) buf, addrlen);

	size = sizeof (dl_uderror_ind_t) + addrlen;

	if ((mp = mexchange(wq, mp, size, M_PCPROTO, DL_UDERROR_IND)) == NULL)
		return;

	dlp = (union DL_primitives *) mp->b_rptr;
	dlp->uderror_ind.dl_dest_addr_length = addrlen;
	dlp->uderror_ind.dl_dest_addr_offset = sizeof (dl_uderror_ind_t);
	dlp->uderror_ind.dl_unix_errno = unix_errno;
	dlp->uderror_ind.dl_errno = errno;
	bcopy((caddr_t) buf,
		(caddr_t) (mp->b_rptr + sizeof (dl_uderror_ind_t)),
		addrlen);
	qreply(wq, mp);
}

void
dlphysaddrack(wq, mp, addrp, len)
queue_t	*wq;
mblk_t	*mp;
caddr_t	addrp;
int	len;
{
	register	union	DL_primitives	*dlp;
	int	size;

	size = sizeof (dl_phys_addr_ack_t) + len;
	if ((mp = mexchange(wq, mp, size, M_PCPROTO, DL_PHYS_ADDR_ACK)) == NULL)
		return;
	dlp = (union DL_primitives *) mp->b_rptr;
	dlp->physaddr_ack.dl_addr_length = len;
	dlp->physaddr_ack.dl_addr_offset = sizeof (dl_phys_addr_ack_t);
	bcopy(addrp, (caddr_t) (mp->b_rptr + sizeof (dl_phys_addr_ack_t)), len);
	qreply(wq, mp);
}
