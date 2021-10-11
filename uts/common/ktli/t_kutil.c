/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)t_kutil.c	1.16	94/01/14 SMI"	/* SVr4.0 1.5  */

/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986,1987,1988,1989  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		  All rights reserved.
 */

/*
 *	Contains the following utility functions:
 *		tli_send:
 *		tli_recv:
 *		get_ok_ack:
 *
 *	Returns:
 *		See individual functions.
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/stream.h>
#include <sys/ioctl.h>
#include <sys/stropts.h>
#include <sys/strsubr.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/tiuser.h>
#include <sys/t_kuser.h>
#include <inet/common.h>
#include <inet/mi.h>
#include <inet/ip.h>

int
tli_send(
	register TIUSER		*tiptr,
	register mblk_t 	*bp,
	register int		fmode)
{
	int			retval;
	register struct file	*fp;
	register struct vnode	*vp;
	register struct stdata	*stp;
	int			error;

	retval = 0;
	error = 0;
	fp = tiptr->fp;
	vp = fp->f_vnode;
	stp = vp->v_stream;

	/*
	 * We call enterq() because putnext assumes we are coming
	 * from a queue that was also entered.
	 */
	enterq(stp->sd_wrq);

	/*
	 * sd_lock prevents a race between the canput and
	 * sleeping in strwaitq.
	 */
	mutex_enter(&stp->sd_lock);
	/*CONSTANTCONDITION*/
	while (1) {
		if (canput(stp->sd_wrq->q_next))
			break;

		if ((error = strwaitq(stp, WRITEWAIT, (off_t)0, fmode,
		    &retval)) != 0 || retval) {
			mutex_exit(&stp->sd_lock);
			leaveq(stp->sd_wrq);
			return (error);
		}
	}
	mutex_exit(&stp->sd_lock);

	putnext(stp->sd_wrq, bp);

	leaveq(stp->sd_wrq);

	return (0);
}

int
tli_recv(
	register TIUSER		*tiptr,
	register mblk_t		**bp,
	register int		fmode)
{
	int			retval;
	register struct file	*fp;
	register struct vnode	*vp;
	register struct stdata	*stp;
	int			error;

	retval = 0;
	error = 0;
	fp = tiptr->fp;
	vp = fp->f_vnode;
	stp = vp->v_stream;

	mutex_enter(&stp->sd_lock);
	if (stp->sd_flag & (STRDERR|STPLEX)) {
		error = (stp->sd_flag & STPLEX) ? EINVAL : stp->sd_rerror;
		mutex_exit(&stp->sd_lock);
		return (error);
	}

	enterq(stp->sd_wrq);

	while (!(*bp = getq(RD(stp->sd_wrq)))) {
		if (stp->sd_flag & STRHUP) {
			mutex_exit(&stp->sd_lock);
			leaveq(stp->sd_wrq);
			return (EIO);
		}
		if ((error = strwaitq(stp, READWAIT, (off_t)0, fmode,
		    &retval)) != 0 || retval) {
			mutex_exit(&stp->sd_lock);
			leaveq(stp->sd_wrq);
			return (error);
		}
	}

	leaveq(stp->sd_wrq);

	if (stp->sd_flag)
		stp->sd_flag &= ~STRPRI;
	mutex_exit(&stp->sd_lock);

	return (0);
}

int
get_ok_ack(
	register TIUSER			*tiptr,
	register int 			type,
	register int 			fmode)
{
	register int			msgsz;
	register union T_primitives	*pptr;
	mblk_t				*bp;
	int				error;

	error = 0;

	/*
	 * wait for ack
	 */
	if ((error = tli_recv(tiptr, &bp, fmode)) != 0)
		return (error);

	if ((msgsz = (bp->b_wptr - bp->b_rptr)) < sizeof (long))
		return (EPROTO);

	pptr = (union T_primitives *)bp->b_rptr;
	switch (pptr->type) {
		case T_OK_ACK:
			if (msgsz < TOKACKSZ ||
			    pptr->ok_ack.CORRECT_prim != type)
				error = EPROTO;
			break;

		case T_ERROR_ACK:
			if (msgsz < TERRORACKSZ) {
				error = EPROTO;
				break;
			}

			if (pptr->error_ack.TLI_error == TSYSERR)
				error = pptr->error_ack.UNIX_error;
			else
				error =
				    t_tlitosyserr(pptr->error_ack.TLI_error);
			break;

		default:
			error = EPROTO;
			break;
	}
	return (error);
}

/*
 * Translate a TLI error into a system error as best we can.
 */
static ushort tli_errs[] = {
		0,		/* no error	*/
		EADDRNOTAVAIL,  /* TBADADDR	*/
		ENOPROTOOPT,    /* TBADOPT	*/
		EACCES,		/* TACCES	*/
		EBADF,		/* TBADF	*/
		EADDRNOTAVAIL,	/* TNOADDR	*/
		EPROTO,		/* TOUTSTATE	*/
		EPROTO,		/* TBADSEQ	*/
		0,		/* TSYSERR - will never get */
		EPROTO,		/* TLOOK - should never be sent by transport */
		EMSGSIZE,	/* TBADDATA	*/
		EMSGSIZE,	/* TBUFOVFLW	*/
		EPROTO,		/* TFLOW	*/
		EWOULDBLOCK,    /* TNODATA	*/
		EPROTO,		/* TNODIS	*/
		EPROTO,		/* TNOUDERR	*/
		EINVAL,		/* TBADFLAG	*/
		EPROTO,		/* TNOREL	*/
		EOPNOTSUPP,	/* TNOTSUPPORT	*/
		EPROTO,		/* TSTATECHNG	*/
};

int
t_tlitosyserr(register int terr)
{
	if (terr > (sizeof (tli_errs) / sizeof (ushort)))
		return (EPROTO);
	return (tli_errs[terr]);
}

/*
 * Notify transport that we are having trouble with this connection.
 * If transport is TCP/IP, IP should delete the IRE and start over.
 */

void
t_kadvise(
	TIUSER		*tiptr,
	u_char		*addr,
	int		addr_len)
{

	struct file	*fp;
	struct vnode	*vp;
	struct stdata	*stp;
	struct iocblk	*iocp;
	ipid_t		*ipid;
	mblk_t		*mp;

	fp = tiptr->fp;
	vp = fp->f_vnode;
	stp = vp->v_stream;

	mp = allocb(sizeof (struct iocblk), BPRI_HI);
	if (!mp)
		return;

	mp->b_datap->db_type = M_IOCTL;
	iocp = (struct iocblk *)mp->b_rptr;
	mp->b_wptr = (u_char *)&iocp[1];
	bzero((caddr_t)iocp, sizeof (*iocp));

	/*
	 * Non TCP/IP transport may return M_IOCNAK to this ioctl.
	 * Reduce risk of stream head matching this ioc_id with some
	 * pending ioctl by setting this ioc_id to 0.
	 */
	iocp->ioc_id = 0;
	iocp->ioc_cmd = IP_IOCTL;
	iocp->ioc_count = sizeof (ipid_t) + addr_len;

	mp->b_cont = allocb(iocp->ioc_count, BPRI_HI);
	if (!mp->b_cont) {
		freeb(mp);
		return;
	}

	ipid = (ipid_t *)mp->b_cont->b_rptr;
	mp->b_cont->b_wptr += iocp->ioc_count;
	bzero((caddr_t)ipid, sizeof (*ipid));
	ipid->ipid_cmd = IP_IOC_IRE_DELETE_NO_REPLY;
	ipid->ipid_ire_type = IRE_ROUTE;
	ipid->ipid_addr_offset = sizeof (ipid_t);
	ipid->ipid_addr_length = addr_len;
	bcopy((caddr_t)addr, (caddr_t)&ipid[1], addr_len);
	putnext(stp->sd_wrq, mp);
}

#ifdef KTLIDEBUG
int ktlilog = 0;

/*
 * Kernel level debugging aid. The global variable "ktlilog" is a bit
 * mask which allows various types of debugging messages to be printed
 * out.
 *
 *	ktlilog & 1 	will cause actual failures to be printed.
 *	ktlilog & 2	will cause informational messages to be
 *			printed.
 */
int
ktli_log(register int level, register char *str, register int a1)
{
	if (level & ktlilog)
		printf(str, a1);
}
#endif
