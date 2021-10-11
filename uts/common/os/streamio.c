/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)streamio.c	1.153	95/09/27 SMI"

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/mode.h>
#include <sys/proc.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/stream.h>
#include <sys/session.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/strstat.h>
#include <sys/var.h>
#include <sys/poll.h>
#include <sys/termio.h>
#include <sys/ttold.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/cmn_err.h>
#include <sys/sad.h>
#include <sys/priocntl.h>
#include <sys/jioctl.h>
#include <sys/procset.h>
#include <sys/session.h>
#include <sys/kmem.h>
#include <sys/filio.h>
#include <sys/vtrace.h>
#include <sys/debug.h>
#include <sys/strredir.h>
#include <sys/fs/fifonode.h>
#ifdef _VPIX
#include <sys/kd.h>
#include <sys/asy.h>
#include <sys/v86intr.h>
#include <sys/v86.h>
#endif


/*
 * id value used to distinguish between different ioctl messages
 */
static long ioc_id;

#if defined(__STDC__)
static void putback(struct stdata *, queue_t *, mblk_t *, int);
#else
static void putback(/* stdata , queue_t, mblk_t , int */);
#endif /* __STDC__ */
/*
 *  Qinit structure and Module_info structures
 *	for stream head read and write queues
 */
int	strrput(), strwsrv();
struct 	module_info strm_info = { 0, "strrhead", 0, INFPSZ, STRHIGH, STRLOW };
struct  module_info stwm_info = { 0, "strwhead", 0, 0, 0, 0 };
struct	qinit strdata = { strrput, NULL, NULL, NULL, NULL, &strm_info, NULL };
struct	qinit stwdata = { NULL, strwsrv, NULL, NULL, NULL, &stwm_info, NULL };
struct	module_info fiform_info = { 0, "fifostrrhead", 0, PIPE_BUF, FIFOHIWAT,
		FIFOLOWAT };
struct  module_info fifowm_info = { 0, "fifostrwhead", 0, 0, 0, 0 };
struct	qinit fifo_strdata = { strrput, NULL, NULL, NULL, NULL, &fiform_info,
		NULL };
struct	qinit fifo_stwdata = { NULL, strwsrv, NULL, NULL, NULL, &fifowm_info,
		NULL };


extern char qrunflag;
extern kmutex_t	strresources;
extern kmutex_t muxifier;
extern kmutex_t sad_lock;
extern run_queues;
extern void time_to_wait();

/*
 * Stream head locking notes:
 *	There are four monitors associated with the stream head:
 *	1. v_stream monitor: in stropen() and strclose() v_lock
 *		is held while the association of vnode and stream
 *		head is established or tested for.
 *	2. open/close/push/pop monitor: sd_lock is held while each
 *		thread bids for exclusive access to this monitor
 *		for opening or closing a stream.  In addition, this
 *		monitor is entered during pushes and pops.  This
 *		guarantees that during plumbing operations there
 *		is only one thread trying to change the plumbing.
 *		Any other threads present in the stream are only
 *		using the plumbing.
 *	3. read/write monitor: in the case of read, a thread holds
 *		sd_lock while trying to get data from the stream
 *		head queue.  if there is none to fulfill a read
 *		request, it sets RSLEEP and calls cv_wait_sig() down
 *		in strwaitq() to await the arrival of new data.
 *		when new data arrives in strrput(), sd_lock is acquired
 *		before testing for RSLEEP and calling cv_broadcast().
 *		the behavior of strwrite(), strwsrv(), and WSLEEP
 *		mirror this.
 *	4. ioctl monitor: sd_lock is gotten to ensure that only one
 *		thread is doing an ioctl at a time.
 */

/*
 * Open a stream device.
 */
int
stropen(vnode_t *vp, dev_t *devp, int flag, cred_t *crp)
{
	register struct stdata *stp;
	register queue_t *qp;
	register int s;
	dev_t dummydev;
	struct autopush *ap;
	int error = 0;
	long	rmin, rmax;

#ifdef C2_AUDIT
	if (audit_active)
		audit_stropen(vp, devp, flag, crp);
#endif

	/*
	 * If the stream already exists, wait for any open in progress
	 * to complete, then call the open function of each module and
	 * driver in the stream.  Otherwise create the stream.
	 */
	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STROPEN, "stropen:%X", vp);
retry:
	mutex_enter(&vp->v_lock);
	if ((stp = vp->v_stream) != NULL) {

		/*
		 * Waiting for stream to be created to device
		 * due to another open.
		 */

	    mutex_exit(&vp->v_lock);

	    if (STRMATED(stp)) {
		struct stdata *strmatep = stp->sd_mate;

		STRLOCKMATES(stp);
		if (strmatep->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
			if (flag & (FNDELAY|FNONBLOCK)) {
				error = EAGAIN;
				mutex_exit(&strmatep->sd_lock);
				goto ckreturn;
			}
			mutex_exit(&stp->sd_lock);
			if (!cv_wait_sig(&strmatep->sd_monitor,
			    &strmatep->sd_lock)) {
				error = EINTR;
				mutex_exit(&strmatep->sd_lock);
				mutex_enter(&stp->sd_lock);
				goto ckreturn;
			}
			mutex_exit(&strmatep->sd_lock);
			goto retry;
		}
		if (stp->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
			if (flag & (FNDELAY|FNONBLOCK)) {
				error = EAGAIN;
				mutex_exit(&strmatep->sd_lock);
				goto ckreturn;
			}
			mutex_exit(&strmatep->sd_lock);
			if (!cv_wait_sig(&stp->sd_monitor, &stp->sd_lock)) {
				error = EINTR;
				goto ckreturn;
			}
			mutex_exit(&stp->sd_lock);
			goto retry;
		}

		if (stp->sd_flag & (STRDERR|STWRERR)) {
			error = EIO;
			mutex_exit(&strmatep->sd_lock);
			goto ckreturn;
		}

		stp->sd_flag |= STWOPEN;
		STRUNLOCKMATES(stp);
	    } else {
		mutex_enter(&stp->sd_lock);
		if (stp->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
			if (flag & (FNDELAY|FNONBLOCK)) {
				error = EAGAIN;
				goto ckreturn;
			}
			if (!cv_wait_sig(&stp->sd_monitor, &stp->sd_lock)) {
				error = EINTR;
				goto ckreturn;
			}
			mutex_exit(&stp->sd_lock);
			goto retry;  /* could be clone! */
		}

		if (stp->sd_flag & (STRDERR|STWRERR)) {
			error = EIO;
			goto ckreturn;
		}

		stp->sd_flag |= STWOPEN;
		mutex_exit(&stp->sd_lock);
	    }

		/*
		 * Open all modules and devices down stream to notify
		 * that another user is streaming.  For modules, set the
		 * last argument to MODOPEN and do not pass any open flags.
		 * Ignore dummydev since this is not the first open.
		 */
	    claimstr(stp->sd_wrq);
	    qp = stp->sd_wrq;
	    while (SAMESTR(qp)) {
		qp = qp->q_next;
		if ((error = qreopen(RD(qp), devp, flag, crp)) != 0)
			break;
	    }
	    releasestr(stp->sd_wrq);
	    mutex_enter(&stp->sd_lock);
	    stp->sd_flag &= ~(STRHUP|STWOPEN);
	    stp->sd_rerror = 0;
	    stp->sd_werror = 0;
ckreturn:
	    cv_broadcast(&stp->sd_monitor);
	    mutex_exit(&stp->sd_lock);
	    return (error);
	}

	/*
	 * This vnode isn't streaming.  SPECFS already
	 * checked for multiple vnodes pointing to the
	 * same stream, so create a stream to the driver.
	 */
	qp = allocq();
	stp = shalloc(qp);

	/*
	 * Initialize stream head.  shalloc() has given us
	 * exclusive access, and we have the vnode locked;
	 * we can do whatever we want with stp.
	 */
	stp->sd_flag = STWOPEN;
	stp->sd_siglist = NULL;
	stp->sd_pollist.ph_list = NULL;
	stp->sd_sigflags = 0;
	stp->sd_mark = NULL;
	stp->sd_closetime = STRTIMOUT;
	stp->sd_sidp = NULL;
	stp->sd_pgidp = NULL;
	stp->sd_vnode = vp;
	stp->sd_rerror = 0;
	stp->sd_werror = 0;
	stp->sd_wroff = 0;
	stp->sd_iocblk = NULL;
	stp->sd_pushcnt = 0;
	stp->sd_qn_minpsz = 0;
	stp->sd_qn_maxpsz = INFPSZ - 1;	/* used to check for intitailization */
	stp->sd_vnfifo = NULL;
	qp->q_ptr = WR(qp)->q_ptr = stp;
	STREAM(qp) = STREAM(WR(qp)) = stp;
	vp->v_stream = stp;
	mutex_exit(&vp->v_lock);
	if (vp->v_type == VFIFO) {
		stp->sd_flag |= OLDNDELAY;
		/* setq might sleep in kmem_alloc - avoid holding locks. */
		setq(qp, &fifo_strdata, &fifo_stwdata, NULL, NULL,
			QMTSAFE, SQ_CI|SQ_CO);

		set_qend(qp);
		stp->sd_strtab = (struct streamtab *)fifo_getinfo();
		WR(qp)->q_nfsrv = WR(qp);
		qp->q_nfsrv = qp;
		WR(qp)->q_nbsrv = qp->q_nbsrv = NULL;
		/*
		 * Wake up others that are waiting for stream to be created.
		 */
		mutex_enter(&stp->sd_lock);
		/*
		 * nothing is be pushed on stream yet, so
		 * optimized stream head packetsizes are just that
		 * of the read queue
		 */
		stp->sd_qn_minpsz = qp->q_minpsz;
		stp->sd_qn_maxpsz = qp->q_maxpsz;
		stp->sd_flag &= ~STWOPEN;
		goto fifo_opendone;
	}
	/* setq might sleep in kmem_alloc - avoid holding locks. */
	setq(qp, &strdata, &stwdata, NULL, NULL, QMTSAFE, SQ_CI|SQ_CO);

	set_qend(qp);

	/*
	 * Open driver and create stream to it (via qattach).
	 */
	dummydev = *devp;
	if (error = qattach(qp, devp, flag, CDEVSW, getmajor(*devp), crp)) {
		mutex_enter(&vp->v_lock);
		vp->v_stream = NULL;
		mutex_exit(&vp->v_lock);
		mutex_enter(&stp->sd_lock);
		cv_broadcast(&stp->sd_monitor);
		mutex_exit(&stp->sd_lock);
		freeq(RD(qp));
		shfree(stp);
		return (error);
	}
	/*
	 * Set sd_strtab after open in order to handle clonable drivers
	 */
	stp->sd_strtab = STREAMSTAB(getmajor(*devp));

	/*
	 * check for autopush
	 */
	mutex_enter(&sad_lock);
	ap = strphash(getemajor(*devp));
	while (ap) {
		if (ap->ap_major == (getemajor(*devp))) {
			if (ap->ap_type == SAP_ALL)
				break;
			else if ((ap->ap_type == SAP_ONE) &&
				    (ap->ap_minor == geteminor(*devp)))
					break;
			else if ((ap->ap_type == SAP_RANGE) &&
				    (geteminor(*devp) >= ap->ap_minor) &&
				    (geteminor(*devp) <= ap->ap_lastminor))
					break;
		}
		ap = ap->ap_nextp;
	}
	if (ap == NULL) {
		mutex_exit(&sad_lock);
		goto opendone;
	}
	ap->ap_cnt++;
	mutex_exit(&sad_lock);
	for (s = 0; s < ap->ap_npush; s++) {

		if (stp->sd_flag & (STRHUP|STRDERR|STWRERR)) {
			error = (stp->sd_flag & STRHUP) ? ENXIO : EIO;
			break;
		}
		if (stp->sd_pushcnt >= nstrpush) {
			error = EINVAL;
			break;
		}

		/*
		 * Note: fmod lock is released at close time.
		 */

		if (!findmodbyindex(ap->ap_list[s])) {
			stp->sd_flag |= STREOPENFAIL;
			error = EINVAL;
			break;
		}
		/*
		* push new module and call its open routine via qattach
		*/
		if (error = qattach(qp, &dummydev, 0, FMODSW, ap->ap_list[s],
		    crp)) {
			(void) fmod_unlock(ap->ap_list[s]);
			break;
		} else {
			mutex_enter(&stp->sd_lock);
			stp->sd_pushcnt++;
			mutex_exit(&stp->sd_lock);
		}

		/*
		* If flow control is on, don't break it - enable
		* first back queue with svc procedure
		*/
		if (RD(stp->sd_wrq)->q_flag & QWANTW) {
			/* Note: no setqback here - use pri -1. */
			backenable(RD(stp->sd_wrq->q_next), -1);
		}
	} /* for */
	mutex_enter(&sad_lock);
	if (--(ap->ap_cnt) <= 0)
		ap_free(ap);
	mutex_exit(&sad_lock);

	/*
	 * let specfs know that open failed part way through
	 */

	if (error) {
		mutex_enter(&stp->sd_lock);
		stp->sd_flag |= STREOPENFAIL;
		mutex_exit(&stp->sd_lock);
	}

opendone:

	/*
	 * Wake up others that are waiting for stream to be created.
	 */
	mutex_enter(&stp->sd_lock);
	stp->sd_flag &= ~STWOPEN;

	/*
	 * As a performance concern we are caching the values of
	 * q_minpsz and q_maxpsz of the module below the stream
	 * head in the stream head.
	 */
	mutex_enter(QLOCK(stp->sd_wrq->q_next));
	rmin = stp->sd_wrq->q_next->q_minpsz;
	rmax = stp->sd_wrq->q_next->q_maxpsz;
	mutex_exit(QLOCK(stp->sd_wrq->q_next));

	/* do this processing here as a performance concern */
	if (strmsgsz != 0) {
		if (rmax == INFPSZ)
			rmax = strmsgsz;
		else
			rmax = MIN(strmsgsz, rmax);
	}

	mutex_enter(QLOCK(stp->sd_wrq));
	stp->sd_qn_minpsz = rmin;
	stp->sd_qn_maxpsz = rmax;
	mutex_exit(QLOCK(stp->sd_wrq));

fifo_opendone:
	cv_broadcast(&stp->sd_monitor);
	mutex_exit(&stp->sd_lock);
	return (error);
}

static int strsink(queue_t *, mblk_t *);
static struct qinit deadrend = {
	strsink, NULL, NULL, NULL, NULL, &strm_info, NULL
};
static struct qinit deadwend = {
	NULL, NULL, NULL, NULL, NULL, &stwm_info, NULL
};

/*
 * Close a stream.
 * This is called from closef() on the last close of an open stream.
 * Strclean() will already have removed the siglist and pollist
 * information, so all that remains is to remove all multiplexor links
 * for the stream, pop all the modules (and the driver), and free the
 * stream structure.
 */

int
strclose(vp, flag, crp)
	struct vnode *vp;
	int flag;
	cred_t *crp;
{
	register struct stdata *stp;
	register queue_t *qp;
	int rval;
	int freestp = 1;
	int mid;
	register queue_t *rmq;
	struct vnode *vnfifo;

#ifdef C2_AUDIT
	if (audit_active)
		audit_strclose(vp, flag, crp);
#endif

	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STRCLOSE, "strclose:%X", vp);
	ASSERT(vp->v_stream);

	stp = vp->v_stream;
	ASSERT(!(stp->sd_flag & STPLEX));
	qp = stp->sd_wrq;

	/* Needed so that strpoll will return non-zero for this fd. */
	mutex_enter(&stp->sd_lock);
	stp->sd_flag |= STRDERR | STWRERR;
	stp->sd_rerror = stp->sd_werror = ENXIO;
	mutex_exit(&stp->sd_lock);

	/*
	 * We call pollwakeup here so that it will remove the remaining
	 * poll data from stream head. Fix for bug 1109605.
	 */
	pollwakeup_safe(&stp->sd_pollist, POLLERR);
	ppwaitemptylist(&stp->sd_pollist);

	ASSERT(stp->sd_pollist.ph_list == NULL);
	ASSERT(stp->sd_sidp == NULL);
	ASSERT(stp->sd_pgidp == NULL);

	if (STRMATED(stp)) {
	    struct stdata *strmatep = stp->sd_mate;
	    int waited = 1;

	    STRLOCKMATES(stp);
	    while (waited) {
		waited = 0;
		while (stp->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
			mutex_exit(&strmatep->sd_lock);
			cv_wait(&stp->sd_monitor, &stp->sd_lock);
			mutex_exit(&stp->sd_lock);
			STRLOCKMATES(stp);
			waited = 1;
		}
		while (strmatep->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
			mutex_exit(&stp->sd_lock);
			cv_wait(&strmatep->sd_monitor, &strmatep->sd_lock);
			mutex_exit(&strmatep->sd_lock);
			STRLOCKMATES(stp);
			waited = 1;
		}
	    }
	    stp->sd_flag |= STRCLOSE;
	    STRUNLOCKMATES(stp);
	} else {
		mutex_enter(&stp->sd_lock);
		stp->sd_flag |= STRCLOSE;
		mutex_exit(&stp->sd_lock);
	}

	ASSERT(qp->q_first == NULL);	/* No more delayed write */

	/* Check if an I_LINK was ever done on this stream */
	if (stp->sd_flag & STRHASLINKS) {
		(void) munlinkall(stp, LINKCLOSE|LINKNORMAL, crp, &rval);
	}

	while (SAMESTR(qp)) {
		/*
		 * Holding sd_lock prevents q_next from changing in
		 * this stream.
		 */
		mutex_enter(&stp->sd_lock);
		if (!(flag & (FNDELAY|FNONBLOCK)) && (stp->sd_closetime > 0)) {

			/*
			 * sleep until awakened by strwsrv() or timeout
			 */
			while (qp->q_next->q_count) {
				stp->sd_flag |= WSLEEP;

				/* ensure strwsrv gets enabled */
				mutex_enter(QLOCK(qp->q_next));
				qp->q_next->q_flag |= QWANTW;
				mutex_exit(QLOCK(qp->q_next));
				/* get out if we timed out or recv'd a signal */
				if (str_cv_wait(&qp->q_wait, &stp->sd_lock,
				    (u_long)stp->sd_closetime, 0) <= 0) {
					break;
				}
			}
			stp->sd_flag &= ~WSLEEP;
			mutex_exit(&stp->sd_lock);
		}
		else
			mutex_exit(&stp->sd_lock);
		rmq = qp->q_next;
		if (!(rmq->q_flag & QISDRV)) {
			mutex_enter(&fmodsw_lock);
			mid = findmodbyname(rmq->q_qinfo->qi_minfo->mi_idname);
			mutex_exit(&fmodsw_lock);
			qdetach(RD(rmq), 1, flag, crp);
			if (mid != -1) {
				register struct fmodsw *fmp;

				fmp = &fmodsw[mid];
				ASSERT(fmp);
				rw_exit(fmp->f_lock);
			}
		} else {
			ASSERT(!SAMESTR(rmq));
			qdetach(RD(rmq), 1, flag, crp);
		}
	}

	/* Prevent qenable from re-enabling the stream head queue */
	disable_svc(RD(qp));

	/*
	 * Remove queues from runlist, if they are enabled.
	 */
	remove_runlist(RD(qp));

	/*
	 * Wait until service procedure of each queue is
	 * run, if QINSERVICE is set.
	 */
	wait_svc(RD(qp));

	/*
	 * Now, flush both queues.
	 * Note that flushq handles M_PASSFP by using freemsg_flush instead
	 * of freemsg.
	 */
	flushq(RD(qp), FLUSHALL);
	flushq(qp, FLUSHALL);

	/*
	 * If the write queue of the stream head is pointing to a
	 * read queue, we have a twisted stream.  If the read queue
	 * is alive, convert the stream head queues into a dead end.
	 * If the read queue is dead, free the dead pair.
	 */
	if (qp->q_next && !SAMESTR(qp)) {
		if (qp->q_next->q_qinfo == &deadrend) {	/* half-closed pipe */
			flushq(qp->q_next, FLUSHALL); /* ensure no message */
			shfree(qp->q_next->q_stream);
			freeq(qp->q_next);
			freeq(RD(qp));
		} else if (qp->q_next == RD(qp)) {	/* fifo */
			freeq(RD(qp));
		} else {				/* pipe */
			freestp = 0;
			/*
			 * The q_info pointers are never accessed when
			 * SQLOCK is held.
			 */
			ASSERT(qp->q_syncq == RD(qp)->q_syncq);
			mutex_enter(SQLOCK(qp->q_syncq));
			qp->q_qinfo = &deadwend;
			RD(qp)->q_qinfo = &deadrend;
			mutex_exit(SQLOCK(qp->q_syncq));
		}
	} else {
		freeq(RD(qp)); /* free stream head queue pair */
	}

	mutex_enter(&vp->v_lock);
	if (stp->sd_iocblk) {
		freemsg(stp->sd_iocblk);
		stp->sd_iocblk = NULL;
	}
	stp->sd_vnode = NULL;
	vp->v_stream = NULL;
	mutex_exit(&vp->v_lock);
	mutex_enter(&stp->sd_lock);
	vnfifo = stp->sd_vnfifo;
	stp->sd_vnfifo = NULL;
	stp->sd_flag &= ~STRCLOSE;
	cv_broadcast(&stp->sd_monitor);
	mutex_exit(&stp->sd_lock);
	/*
	 * Release held FIFO vnode
	 * Note: VN_RELE acquires v_lock.
	 */
	if (vnfifo) {
		VN_RELE(vnfifo);
	}

	if (freestp)
		shfree(stp);
	return (0);
}

static int
strsink(q, bp)
	register queue_t *q;
	register mblk_t *bp;
{
	struct copyresp *resp;

	switch (bp->b_datap->db_type) {
	case M_FLUSH:
		if ((*bp->b_rptr & FLUSHW) && !(bp->b_flag & MSGNOLOOP)) {
			*bp->b_rptr &= ~FLUSHR;
			bp->b_flag |= MSGNOLOOP;
			/*
			 * Protect against the driver passing up
			 * messages after it has done a qprocsoff.
			 */
			if (OTHERQ(q)->q_next == NULL)
				freemsg(bp);
			else
				qreply(q, bp);
		} else {
			freemsg(bp);
		}
		break;

	case M_COPYIN:
	case M_COPYOUT:
		if (bp->b_cont) {
			freemsg(bp->b_cont);
			bp->b_cont = NULL;
		}
		bp->b_datap->db_type = M_IOCDATA;
		resp = (struct copyresp *)bp->b_rptr;
		resp->cp_rval = (caddr_t)1;	/* failure */
		/*
		 * Protect against the driver passing up
		 * messages after it has done a qprocsoff.
		 */
		if (OTHERQ(q)->q_next == NULL)
			freemsg(bp);
		else
			qreply(q, bp);
		break;

	case M_IOCTL:
		if (bp->b_cont) {
			freemsg(bp->b_cont);
			bp->b_cont = NULL;
		}
		bp->b_datap->db_type = M_IOCNAK;
		/*
		 * Protect against the driver passing up
		 * messages after it has done a qprocsoff.
		 */
		if (OTHERQ(q)->q_next == NULL)
			freemsg(bp);
		else
			qreply(q, bp);
		break;

	default:
		freemsg(bp);
		break;
	}

	return (0);
}

/*
 * Clean up after a process when it closes a stream.  This is called
 * from closef for all closes, whereas strclose is called only for the
 * last close on a stream.  The siglist is scanned for entries for the
 * current process, and these are removed.
 */
void
strclean(vp)
	struct vnode *vp;
{
	strsig_t *ssp, *pssp, *tssp;
	stdata_t *stp;
	int update = 0;

	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STRCLEAN, "strclean:%X", vp);
	stp = vp->v_stream;
	pssp = NULL;
	mutex_enter(&stp->sd_lock);
	ssp = stp->sd_siglist;
	while (ssp) {
		if (ssp->ss_pidp == curproc->p_pidp) {
			tssp = ssp->ss_next;
			if (pssp)
				pssp->ss_next = tssp;
			else
				stp->sd_siglist = tssp;
			mutex_enter(&pidlock);
			PID_RELE(ssp->ss_pidp);
			mutex_exit(&pidlock);
			kmem_cache_free(strsig_cache, ssp);
			update = 1;
			ssp = tssp;
		} else {
			pssp = ssp;
			ssp = ssp->ss_next;
		}
	}
	if (update) {
		stp->sd_sigflags = 0;
		for (ssp = stp->sd_siglist; ssp; ssp = ssp->ss_next)
			stp->sd_sigflags |= ssp->ss_events;
	}
	mutex_exit(&stp->sd_lock);
}

/*
 * Read a stream according to the mode flags in sd_flag:
 *
 * (default mode)		- Byte stream, msg boundries are ignored
 * RMSGDIS (msg discard)	- Read on msg boundries and throw away
 *				any data remaining in msg
 * RMSGNODIS (msg non-discard)	- Read on msg boundries and put back
 *				any remaining data on head of read queue
 *
 * Consume readable messages on the front of the queue until
 * ttolwp(curthread)->lwp_count
 * is satisfied, the readable messages are exhausted, or a message
 * boundary is reached in a message mode.  If no data was read and
 * the stream was not opened with the NDELAY flag, block until data arrives.
 * Otherwise return the data read and update the count.
 *
 * In default mode a 0 length message signifies end-of-file and terminates
 * a read in progress.  The 0 length message is removed from the queue
 * only if it is the only message read (no data is read).
 *
 * An attempt to read an M_PROTO or M_PCPROTO message results in an
 * EBADMSG error return, unless either RDPROTDAT or RDPROTDIS are set.
 * If RDPROTDAT is set, M_PROTO and M_PCPROTO messages are read as data.
 * If RDPROTDIS is set, the M_PROTO and M_PCPROTO parts of the message
 * are unlinked from and M_DATA blocks in the message, the protos are
 * thrown away, and the data is read.
 */
/* ARGSUSED */
int
strread(vp, uiop, crp)
	struct vnode *vp;
	struct uio *uiop;
	cred_t *crp;
{
	register struct stdata *stp;
	register mblk_t *bp, *nbp;
	register queue_t *q;
	int n;
	int done = 0;
	int error = 0;
	void *canrw;
	char rflg;
	char rwflg;
	short mark;
	short delim;
	unsigned char pri;
	char waitflag;

	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STRREAD_ENTER, "strread:%X", vp);
	ASSERT(vp->v_stream);
	stp = vp->v_stream;
	canrw = stp->sd_struiordq;

	if (stp->sd_sidp != NULL && stp->sd_vnode->v_type != VFIFO)
		if (error = straccess(stp, JCREAD))
			goto oops1;
	mutex_enter(&stp->sd_lock);
	if (stp->sd_flag & (STRDERR|STPLEX)) {
		error = strgeterr(stp, STRDERR|STPLEX);
		goto oops;
	}

	/*
	 * Loop terminates when uiop->uio_resid == 0.
	 */
	rflg = 0;
	rwflg = 0;
	waitflag = READWAIT;
	q = RD(stp->sd_wrq);
	for (;;) {
		ASSERT(MUTEX_HELD(&stp->sd_lock));
		mark = 0;
		delim = 0;
		while (!(bp = getq(q))) {
			if (canrw && rwflg) {
				/*
				 * Stream supports rwnext() for the read side.
				 */
				struiod_t uiod;

				uiodup(uiop, &uiod.d_uio, uiod.d_iov,
				    sizeof (uiod.d_iov) / sizeof (*uiod.d_iov));
				uiod.d_mp = 0;
				stp->sd_struiodnak++;
				error = rwnext(q, &uiod);
				mutex_enter(&stp->sd_lock);
				stp->sd_struiodnak--;
				while (!stp->sd_struiodnak &&
				    (bp = stp->sd_struionak)) {
					stp->sd_struionak = bp->b_next;
					bp->b_next = NULL;
					bp->b_datap->db_type = M_IOCNAK;
					if (OTHERQ(q)->q_next == NULL)
						freemsg(bp);
					else {
						mutex_exit(&stp->sd_lock);
						qreply(q, bp);
						mutex_enter(&stp->sd_lock);
					}
				}
				if (! error) {
					if ((bp = uiod.d_mp) != NULL) {
						mutex_exit(&stp->sd_lock);
						rwflg = 0;
						pri = 0;
						goto ismdata;
					}
					if (bp = getq(q))
						/*
						 * A rwnext() generated mblk
						 * has bubbled up via strrput().
						 */
						break;
				} else if (error == EINVAL) {
					/*
					 * The stream plumbing must have
					 * changed while we where away, so
					 * just turn off rwnext()s.
					 */
					error = 0;
					canrw = 0;
					if (bp = getq(q))
						/*
						 * A rwnext() generated mblk
						 * has bubbled up via strrput().
						 */
						break;
				} else if (error == EBUSY) {
					error = 0;
					if (bp = getq(q))
						/*
						 * A rwnext() generated mblk
						 * has bubbled up via strrput().
						 */
						break;
				} else
					goto oops;
			}
			if (stp->sd_flag & STRHUP) {
				goto oops;
			}
			if (rflg && !(stp->sd_flag & STRDELIM)) {
				goto oops;
			}

			TRACE_3(TR_FAC_STREAMS_FR,
				TR_STRREAD_WAIT,
				"strread calls strwaitq:%X, %X, %X",
						vp, uiop, crp);
			if ((error = strwaitq(stp, waitflag, uiop->uio_resid,
			    uiop->uio_fmode, &done)) != 0 || done) {
				TRACE_3(TR_FAC_STREAMS_FR,
					TR_STRREAD_DONE,
					"strread error or done:%X, %X, %X",
					vp, uiop, crp);
				if ((uiop->uio_fmode & FNDELAY) &&
				    (stp->sd_flag & OLDNDELAY) &&
				    (error == EAGAIN))
					error = 0;
				goto oops;
			}
			TRACE_3(TR_FAC_STREAMS_FR, TR_STRREAD_AWAKE,
				"strread awakes:%X, %X, %X", vp, uiop, crp);
			/*
			 * Turn back on rwnext().
			 */
			rwflg = 1;
		}
		pri = bp->b_band;
		if (stp->sd_mark == bp) {
			if (rflg) {
				putback(stp, q, bp, pri);
				goto oops;
			}
			mark = 1;
			stp->sd_mark = NULL;
		}
		if ((stp->sd_flag & STRDELIM) && (bp->b_flag & MSGDELIM))
			delim = 1;
		mutex_exit(&stp->sd_lock);

		if (qready())
			queuerun();

		switch (bp->b_datap->db_type) {

		case M_DATA:
ismdata:
			if (msgdsize(bp) == 0) {
				if (mark || delim) {
					freemsg(bp);
				} else if (rflg) {

					/*
					 * If already read data put zero
					 * length message back on queue else
					 * free msg and return 0.
					 */
					bp->b_band = pri;
					mutex_enter(&stp->sd_lock);
					putback(stp, q, bp, pri);
					mutex_exit(&stp->sd_lock);
				} else {
					freemsg(bp);
				}
				error =  0;
				goto oops1;
			}

			rflg = 1;
			waitflag |= NOINTR;
			while (bp && uiop->uio_resid) {
				dblk_t *dp = bp->b_datap;

				if (canrw &&
				    (dp->db_struioflag & STRUIO_SPEC) &&
				    (bp->b_rptr <= dp->db_struiobase) &&
				    (bp->b_wptr >= dp->db_struiolim)) {
					/*
					 * This is an mblk that may have had
					 * part of it uiomove()ed already, so
					 * we have to handle up to three cases:
					 *
					 *    1) data prefixed to the uio data
					 *	     rptr thru (uiobase - 1)
					 *    2) uio data already uiomove()ed
					 *	     uiobase thru (uioptr - 1)
					 *    3) uio data not uiomove()ed and
					 *	 data suffixed to the uio data
					 *	     uioptr thru (wptr - 1)
					 *
					 * That is, this mblk may be proccesed
					 * up to three times, one for each case.
					 */
					if ((n = dp->db_struiobase
					/* LINTED - statement has no conseq */
					    - bp->b_rptr) > 0) {
						/*
						 * Prefixed data.
						 */
						;
					} else if ((n = dp->db_struioptr
					    - dp->db_struiobase) > 0) {
						/*
						 * Uio data already uiomove()ed.
						 */
						ASSERT(n <= uiop->uio_resid);
						ASSERT(dp->db_struiobase
							== bp->b_rptr);
						dp->db_struiobase += n;
						uioskip(uiop, n);
						goto skip;
					} else if ((n = bp->b_wptr
					    - dp->db_struioptr) > 0) {
						/*
						 * Uio data not uiomove()ed
						 * and/or suffixed data.
						 */
						ASSERT(dp->db_struioptr
							== bp->b_rptr);
						dp->db_struiobase += n;
						dp->db_struioptr += n;
					} else if ((n = bp->b_wptr
					    - bp->b_rptr) > 0) {
						/*
						 * If n is +, one of the above
						 * branches should have been
						 * taken.  If it is -, this
						 * is a bad thing!
						 */
						cmn_err(CE_PANIC,
						    "strread: STRUIO %x %x",
						    (int)bp,
						    (int)uiop->uio_resid);
					} else {
						/*
						 * Zero length mblk, skip it.
						 */
						goto skip;
					}
				} else {
					n = bp->b_wptr - bp->b_rptr;
				}
				if ((n = MIN(uiop->uio_resid, n)) != 0) {
					ASSERT(n > 0);
					error = uiomove((char *)bp->b_rptr,
						n, UIO_READ, uiop);
				}

				if (error) {
					freemsg(bp);
					goto oops1;
				}
			skip:;
				bp->b_rptr += n;
				while (bp && (bp->b_rptr >= bp->b_wptr)) {
					nbp = bp;
					bp = bp->b_cont;
					freeb(nbp);
				}
			}

			/*
			 * The data may have been the leftover of a PCPROTO, so
			 * if none is left reset the STRPRI flag just in case.
			 */
			mutex_enter(&stp->sd_lock);
			if (bp) {
				/*
				 * Have remaining data in message.
				 * Free msg if in discard mode.
				 */
				if (stp->sd_flag & RMSGDIS) {
					freemsg(bp);
					stp->sd_flag &= ~STRPRI;
				} else {
					bp->b_band = pri;
					if (mark && !stp->sd_mark) {
						stp->sd_mark = bp;
						bp->b_flag |= MSGMARK;
					}
					if (delim)
						bp->b_flag |= MSGDELIM;
					nbp = bp;
					do {
						nbp->b_datap->db_struioflag = 0;
					} while ((nbp = nbp->b_cont) != NULL);
					if (msgdsize(bp) == 0)
						freemsg(bp);
					else
						putback(stp, q, bp, pri);
				}
			} else {
				stp->sd_flag &= ~STRPRI;
			}

			/*
			 * Check for signal messages at the front of the read
			 * queue and generate the signal(s) if appropriate.
			 * The only signal that can be on queue is M_SIG at
			 * this point.
			 */
			while ((((bp = q->q_first)) != 0) &&
				(bp->b_datap->db_type == M_SIG)) {
					bp = getq(q);
					mutex_exit(&stp->sd_lock);
					strsignal(stp, *bp->b_rptr,
							(long)bp->b_band);
					freemsg(bp);
					if (qready()) {
						queuerun();
					}
					mutex_enter(&stp->sd_lock);
			}

			if ((uiop->uio_resid == 0) || mark || delim ||
			    (stp->sd_flag & (RMSGDIS|RMSGNODIS))) {
				goto oops;
			}
			continue;

		case M_SIG:
			strsignal(stp, *bp->b_rptr, (long)bp->b_band);
			freemsg(bp);
			mutex_enter(&stp->sd_lock);
			continue;

		case M_PROTO:
		case M_PCPROTO:
			/*
			 * Only data messages are readable.
			 * Any others generate an error, unless
			 * RDPROTDIS or RDPROTDAT is set.
			 */
			if (stp->sd_flag & RDPROTDAT) {
				for (nbp = bp; nbp; nbp = bp->b_next) {
				    if ((bp->b_datap->db_type == M_PROTO) ||
					(bp->b_datap->db_type == M_PCPROTO))
					nbp->b_datap->db_type = M_DATA;
				    else
					break;
				}
				mutex_enter(&stp->sd_lock);
				stp->sd_flag &= ~STRPRI;
				mutex_exit(&stp->sd_lock);
				goto ismdata;
			} else if (stp->sd_flag & RDPROTDIS) {
				while (bp &&
				    ((bp->b_datap->db_type == M_PROTO) ||
				    (bp->b_datap->db_type == M_PCPROTO))) {
					nbp = unlinkb(bp);
					freeb(bp);
					bp = nbp;
				}
				mutex_enter(&stp->sd_lock);
				stp->sd_flag &= ~STRPRI;
				mutex_exit(&stp->sd_lock);
				if (bp) {
					bp->b_band = pri;
					goto ismdata;
				} else {
					break;
				}
			}
			/* FALLTHRU */
		case M_PASSFP:
			if ((bp->b_datap->db_type == M_PASSFP) &&
			    (stp->sd_flag & RDPROTDIS)) {
				file_t *fp =
				    ((struct strrecvfd *)bp->b_rptr)->f.fp;

				(void) closef(fp);
				freemsg(bp);
				break;
			}
			mutex_enter(&stp->sd_lock);
			putback(stp, q, bp, pri);
			mutex_exit(&stp->sd_lock);
			if (rflg == 0)
				error = EBADMSG;
			goto oops1;

		default:
			/*
			 * Garbage on stream head read queue.
			 */
			cmn_err(CE_WARN, "bad %x found at stream head\n",
				bp->b_datap->db_type);
			freemsg(bp);
			goto oops1;
		}
		mutex_enter(&stp->sd_lock);
	}
oops:
	mutex_exit(&stp->sd_lock);
oops1:
	return (error);
}

/*
 * Stream read put procedure.  Called from downstream driver/module
 * with messages for the stream head.  Data, protocol, and in-stream
 * signal messages are placed on the queue, others are handled directly.
 */

int
strrput(q, bp)
	register queue_t *q;
	register mblk_t *bp;
{
	register struct stdata *stp;
	register struct iocblk *iocbp;
	struct stroptions *sop;
	struct copyreq *reqp;
	struct copyresp *resp;
	unsigned char bpri;
	qband_t *qbp;

	stp = (struct stdata *)q->q_ptr;

	ASSERT(qclaimed(q));
	TRACE_4(TR_FAC_STREAMS_FR,
		TR_STRRPUT_ENTER,
		"strrput called with message type:q %X bp %X type %X flag %X",
		q, bp, bp->b_datap->db_type, bp->b_flag);

	switch (bp->b_datap->db_type) {

	case M_PCPROTO:
		mutex_enter(&stp->sd_lock);
		/*
		 * Only one priority protocol message is allowed at the
		 * stream head at a time.
		 */
		if (stp->sd_flag & STRPRI) {
			TRACE_0(TR_FAC_STREAMS_FR, TR_STRRPUT_PROTERR,
			    "M_PCPROTO already at head");
			freemsg(bp);
			mutex_exit(&stp->sd_lock);
			return (0);
		}

		/*
		 * Marking doesn't work well when messages
		 * are marked in more than one band.  We only
		 * remember the last message received, even if
		 * it is placed on the queue ahead of other
		 * marked messages.
		 */
		if (bp->b_flag & MSGMARK)
			stp->sd_mark = bp;

		/*
		 * Wake sleeping read/getmsg and cancel deferred wakeup
		 */
		(void) putq(q, bp);
		if (stp->sd_flag & RSLEEP) {
			stp->sd_flag &= ~RSLEEP;
			cv_broadcast(&q->q_wait);
		}
		stp->sd_wakeq &= ~RSLEEP;
		stp->sd_flag |= STRPRI;
		if (stp->sd_sigflags & S_HIPRI)
			strsendsig(stp->sd_siglist, S_HIPRI, 0L);
		mutex_exit(&stp->sd_lock);
		pollwakeup_safe(&stp->sd_pollist, POLLPRI);
		return (0);

	case M_DATA:
	case M_PROTO:
	case M_PASSFP:
		mutex_enter(&stp->sd_lock);
		/*
		 * Marking doesn't work well when messages
		 * are marked in more than one band.  We only
		 * remember the last message received, even if
		 * it is placed on the queue ahead of other
		 * marked messages.
		 */
		if (bp->b_flag & MSGMARK)
			stp->sd_mark = bp;

		/*
		 * Wake sleeping read/getmsg and cancel deferred wakeup
		 */
		(void) putq(q, bp);
		if (stp->sd_flag & RSLEEP) {
			stp->sd_flag &= ~RSLEEP;
			cv_broadcast(&q->q_wait);
		}
		stp->sd_wakeq &= ~RSLEEP;

		if (q->q_first == bp) {
			if (stp->sd_sigflags) {
				if (stp->sd_sigflags & S_INPUT)
					strsendsig(stp->sd_siglist, S_INPUT,
					    (long)bp->b_band);
				if (bp->b_band == 0) {
				    if (stp->sd_sigflags & S_RDNORM)
					    strsendsig(stp->sd_siglist,
						S_RDNORM, 0L);
				} else {
				    if (stp->sd_sigflags & S_RDBAND)
					    strsendsig(stp->sd_siglist,
						S_RDBAND, (long)bp->b_band);
				}
			}
			if (bp->b_band == 0) {
				if (stp->sd_flag & STRPOLL) {
					stp->sd_flag &= ~STRPOLL;
					mutex_exit(&stp->sd_lock);
					pollwakeup_safe(&stp->sd_pollist,
						POLLIN | POLLRDNORM);
				} else
					mutex_exit(&stp->sd_lock);
			} else {
				mutex_exit(&stp->sd_lock);
				pollwakeup_safe(&stp->sd_pollist,
				    POLLIN | POLLRDBAND);
			}
		} else
			mutex_exit(&stp->sd_lock);
		return (0);

	case M_ERROR:
		/*
		 * An error has occured downstream, the errno is in the first
		 * bytes of the message.
		 */
		if ((bp->b_wptr - bp->b_rptr) == 2) {	/* New flavor */
			unsigned char rw = 0;

			mutex_enter(&stp->sd_lock);
			if (*bp->b_rptr != NOERROR) {	/* read error */
				if (*bp->b_rptr != 0) {
					stp->sd_flag |= STRDERR;
					rw |= FLUSHR;
				} else {
					stp->sd_flag &= ~STRDERR;
				}
				stp->sd_rerror = *bp->b_rptr;
			}
			bp->b_rptr++;
			if (*bp->b_rptr != NOERROR) {	/* write error */
				if (*bp->b_rptr != 0) {
					stp->sd_flag |= STWRERR;
					rw |= FLUSHW;
				} else {
					stp->sd_flag &= ~STWRERR;
				}
				stp->sd_werror = *bp->b_rptr;
			}
			if (rw) {
				TRACE_2(TR_FAC_STREAMS_FR,
					TR_STRRPUT_WAKE,
					"strrput cv_broadcast:q %X, bp %X",
					q, bp);
				cv_broadcast(&q->q_wait); /* readers */
				cv_broadcast(&WR(q)->q_wait); /* writers */
				cv_broadcast(&stp->sd_monitor); /* ioctllers */

				if (stp->sd_sigflags & S_ERROR)
					strsendsig(stp->sd_siglist, S_ERROR,
					    ((rw & FLUSHR) ?
					    (long)stp->sd_rerror :
					    (long)stp->sd_werror));
				mutex_exit(&stp->sd_lock);
				pollwakeup_safe(&stp->sd_pollist, POLLERR);

				bp->b_datap->db_type = M_FLUSH;
				*bp->b_rptr = rw;
				bp->b_wptr = bp->b_rptr + 1;
				/*
				 * Protect against the driver passing up
				 * messages after it has done a qprocsoff.
				 */
				if (OTHERQ(q)->q_next == NULL)
					freemsg(bp);
				else
					qreply(q, bp);
				return (0);
			} else
				mutex_exit(&stp->sd_lock);
		} else if (*bp->b_rptr != 0) {		/* Old flavor */
			mutex_enter(&stp->sd_lock);
			stp->sd_flag |= (STRDERR|STWRERR);
			stp->sd_rerror = *bp->b_rptr;
			stp->sd_werror = *bp->b_rptr;
			TRACE_2(TR_FAC_STREAMS_FR,
				TR_STRRPUT_WAKE2,
				"strrput wakeup #2:q %X, bp %X", q, bp);
			cv_broadcast(&q->q_wait); /* the readers */
			cv_broadcast(&WR(q)->q_wait); /* the writers */
			cv_broadcast(&stp->sd_monitor); /* the ioctllers */

			if (stp->sd_sigflags & S_ERROR)
				strsendsig(stp->sd_siglist, S_ERROR,
				    (stp->sd_werror ? (long)stp->sd_werror :
				    (long)stp->sd_rerror));
			mutex_exit(&stp->sd_lock);
			pollwakeup_safe(&stp->sd_pollist, POLLERR);

			bp->b_datap->db_type = M_FLUSH;
			*bp->b_rptr = FLUSHRW;
			/*
			 * Protect against the driver passing up
			 * messages after it has done a qprocsoff.
			 */
			if (OTHERQ(q)->q_next == NULL)
				freemsg(bp);
			else
				qreply(q, bp);
			return (0);
		}
		freemsg(bp);
		return (0);

	case M_HANGUP:

		freemsg(bp);
		mutex_enter(&stp->sd_lock);
		stp->sd_werror = ENXIO;
		stp->sd_flag |= STRHUP;
		stp->sd_flag &= ~(WSLEEP|RSLEEP);

		/*
		 * send signal if controlling tty
		 */

		if (stp->sd_sidp) {
			prsignal(stp->sd_sidp, SIGHUP);
			if (stp->sd_sidp != stp->sd_pgidp)
				pgsignal(stp->sd_pgidp, SIGTSTP);
		}

		/*
		 * wake up read, write, and exception pollers and
		 * reset wakeup mechanism.
		 */
		cv_broadcast(&q->q_wait);	/* the readers */
		cv_broadcast(&WR(q)->q_wait);	/* the writers */
		cv_broadcast(&stp->sd_monitor);	/* the ioctllers */
		mutex_exit(&stp->sd_lock);
		strhup(stp);
		return (0);

	case M_UNHANGUP:
		freemsg(bp);
		mutex_enter(&stp->sd_lock);
		stp->sd_werror = 0;
		stp->sd_flag &= ~STRHUP;
		mutex_exit(&stp->sd_lock);
		return (0);

	case M_SIG:
		/*
		 * Someone downstream wants to post a signal.  The
		 * signal to post is contained in the first byte of the
		 * message.  If the message would go on the front of
		 * the queue, send a signal to the process group
		 * (if not SIGPOLL) or to the siglist processes
		 * (SIGPOLL).  If something is already on the queue,
		 * OR if we are delivering a delayed suspend (*sigh*
		 * another "tty" hack) and there's no one sleeping already,
		 * just enqueue the message.
		 */
		mutex_enter(&stp->sd_lock);
		if (q->q_first || (*bp->b_rptr == SIGTSTP &&
		    !(stp->sd_flag & RSLEEP))) {
			(void) putq(q, bp);
			mutex_exit(&stp->sd_lock);
			return (0);
		}
		mutex_exit(&stp->sd_lock);
		/* FALLTHRU */

	case M_PCSIG:
		/*
		 * Don't enqueue, just post the signal.
		 */
		strsignal(stp, *bp->b_rptr, 0L);
		freemsg(bp);
		return (0);
#ifdef _VPIX
	case M_VPIXINT:
		/*
		 * Post a pseudorupt to a VP/ix process on behalf of a
		 * downstream module.  There is no need to queue this
		 * request because the module sent the data before this
		 * packet.
		 */
		{	v86msg_t *v86m = (v86msg_t *)bp->b_rptr;

			v86deliver(v86m->v86m_i, v86m->v86m_m);
			freemsg(bp);
		}
		return;
#endif
	case M_FLUSH:
		/*
		 * Flush queues.  The indication of which queues to flush
		 * is in the first byte of the message.  If the read queue
		 * is specified, then flush it.  If FLUSHBAND is set, just
		 * flush the band specified by the second byte of the message.
		 *
		 * Note that flushq/flushband handles M_PASSFP by using
		 * freemsg_flush instead of freemsg.
		 */

		if (*bp->b_rptr & FLUSHR) {
			mutex_enter(&stp->sd_lock);
			if (*bp->b_rptr & FLUSHBAND) {
				ASSERT((bp->b_wptr - bp->b_rptr) >= 2);
				flushband(q, *(bp->b_rptr + 1), FLUSHALL);
			} else
				flushq(q, FLUSHALL);
			mutex_exit(&stp->sd_lock);
		}
		if ((*bp->b_rptr & FLUSHW) && !(bp->b_flag & MSGNOLOOP)) {
			*bp->b_rptr &= ~FLUSHR;
			bp->b_flag |= MSGNOLOOP;
			/*
			 * Protect against the driver passing up
			 * messages after it has done a qprocsoff.
			 */
			if (OTHERQ(q)->q_next == NULL)
				freemsg(bp);
			else
				qreply(q, bp);
			return (0);
		}
		freemsg(bp);
		return (0);

	case M_IOCACK:
	case M_IOCNAK:
		iocbp = (struct iocblk *)bp->b_rptr;
		/*
		 * If not waiting for ACK or NAK then just free msg.
		 * If already have ACK or NAK for user then just free msg.
		 * If incorrect id sequence number then just free msg.
		 */
		mutex_enter(&stp->sd_lock);
		if ((stp->sd_flag & IOCWAIT) == 0 || stp->sd_iocblk ||
		    (stp->sd_iocid != iocbp->ioc_id)) {
			freemsg(bp);
			mutex_exit(&stp->sd_lock);
			return (0);
		}

		/*
		 * Assign ACK or NAK to user and wake up.
		 */
		stp->sd_iocblk = bp;
		cv_broadcast(&stp->sd_monitor);
		mutex_exit(&stp->sd_lock);
		return (0);

	case M_COPYIN:
	case M_COPYOUT:
		reqp = (struct copyreq *)bp->b_rptr;

		/*
		 * If not waiting for ACK or NAK then just fail request.
		 * If already have ACK, NAK, or copy request, then just
		 * fail request.
		 * If incorrect id sequence number then just fail request.
		 */
		mutex_enter(&stp->sd_lock);
		if ((stp->sd_flag & IOCWAIT) == 0 || stp->sd_iocblk ||
		    (stp->sd_iocid != reqp->cq_id)) {
			if (bp->b_cont) {
				freemsg(bp->b_cont);
				bp->b_cont = NULL;
			}
			bp->b_datap->db_type = M_IOCDATA;
			resp = (struct copyresp *)bp->b_rptr;
			resp->cp_rval = (caddr_t)1;	/* failure */
			mutex_exit(&stp->sd_lock);
			putnext(stp->sd_wrq, bp);
			return (0);
		}

		/*
		 * Assign copy request to user and wake up.
		 */
		stp->sd_iocblk = bp;
		cv_broadcast(&stp->sd_monitor);
		mutex_exit(&stp->sd_lock);
		return (0);

	case M_SETOPTS:
		/*
		 * Set stream head options (read option, write offset,
		 * min/max packet size, and/or high/low water marks for
		 * the read side only).
		 */

		bpri = 0;
		sop = (struct stroptions *)bp->b_rptr;
		mutex_enter(&stp->sd_lock);
		if (sop->so_flags & SO_READOPT) {
			switch (sop->so_readopt & RMODEMASK) {
			case RNORM:
				stp->sd_flag &= ~(RMSGDIS | RMSGNODIS);
				break;

			case RMSGD:
				stp->sd_flag = ((stp->sd_flag & ~RMSGNODIS) |
				    RMSGDIS);
				break;

			case RMSGN:
				stp->sd_flag = ((stp->sd_flag & ~RMSGDIS) |
				    RMSGNODIS);
				break;
			}
			switch (sop->so_readopt & RPROTMASK) {
			case RPROTNORM:
				stp->sd_flag &= ~(RDPROTDAT | RDPROTDIS);
				break;

			case RPROTDAT:
				stp->sd_flag = ((stp->sd_flag & ~RDPROTDIS) |
				    RDPROTDAT);
				break;

			case RPROTDIS:
				stp->sd_flag = ((stp->sd_flag & ~RDPROTDAT) |
				    RDPROTDIS);
				break;
			}
		}
		if (sop->so_flags & SO_ERROPT) {
			switch (sop->so_erropt & RERRMASK) {
			case RERRNORM:
				stp->sd_flag &= ~STRDERRNONPERSIST;
				break;
			case RERRNONPERSIST:
				stp->sd_flag |= STRDERRNONPERSIST;
				break;
			}
			switch (sop->so_erropt & WERRMASK) {
			case WERRNORM:
				stp->sd_flag &= ~STWRERRNONPERSIST;
				break;
			case WERRNONPERSIST:
				stp->sd_flag |= STWRERRNONPERSIST;
				break;
			}
		}
		if (sop->so_flags & SO_WROFF)
			stp->sd_wroff = sop->so_wroff;
		if (sop->so_flags & SO_MINPSZ)
			q->q_minpsz = sop->so_minpsz;
		if (sop->so_flags & SO_MAXPSZ)
			q->q_maxpsz = sop->so_maxpsz;
		if (sop->so_flags & SO_HIWAT) {
		    if (sop->so_flags & SO_BAND) {
			if (strqset(q, QHIWAT, sop->so_band, sop->so_hiwat))
				cmn_err(CE_WARN,
				    "strrput: could not allocate qband\n");
			else
				bpri = sop->so_band;
		    } else {
			q->q_hiwat = sop->so_hiwat;
		    }
		}
		if (sop->so_flags & SO_LOWAT) {
		    if (sop->so_flags & SO_BAND) {
			if (strqset(q, QLOWAT, sop->so_band, sop->so_lowat))
				cmn_err(CE_WARN,
				    "strrput: could not allocate qband\n");
			else
				bpri = sop->so_band;
		    } else {
			q->q_lowat = sop->so_lowat;
		    }
		}
		if (sop->so_flags & SO_MREADON)
			stp->sd_flag |= SNDMREAD;
		if (sop->so_flags & SO_MREADOFF)
			stp->sd_flag &= ~SNDMREAD;
		if (sop->so_flags & SO_NDELON)
			stp->sd_flag |= OLDNDELAY;
		if (sop->so_flags & SO_NDELOFF)
			stp->sd_flag &= ~OLDNDELAY;
		if (sop->so_flags & SO_ISTTY)
			stp->sd_flag |= STRISTTY;
		if (sop->so_flags & SO_ISNTTY)
			stp->sd_flag &= ~STRISTTY;
		if (sop->so_flags & SO_TOSTOP)
			stp->sd_flag |= STRTOSTOP;
		if (sop->so_flags & SO_TONSTOP)
			stp->sd_flag &= ~STRTOSTOP;
		if (sop->so_flags & SO_DELIM)
			stp->sd_flag |= STRDELIM;
		if (sop->so_flags & SO_NODELIM)
			stp->sd_flag &= ~STRDELIM;

		freemsg(bp);

		mutex_enter(QLOCK(q));
		if (bpri == 0) {
			if ((q->q_count <= q->q_lowat) &&
			    (q->q_flag & QWANTW)) {

				q->q_flag &= ~QWANTW;
				mutex_exit(QLOCK(q));

				backenable(q, bpri);
			} else
				mutex_exit(QLOCK(q));
		} else {
			unsigned char i;

			ASSERT(MUTEX_HELD(QLOCK(q)));
			qbp = q->q_bandp;
			for (i = 1; i < bpri; i++)
				qbp = qbp->qb_next;
			if ((qbp->qb_count <= qbp->qb_lowat) &&
			    (qbp->qb_flag & QB_WANTW)) {
				qbp->qb_flag &= ~QB_WANTW;
				mutex_exit(QLOCK(q));

				backenable(q, bpri);
			} else
				mutex_exit(QLOCK(q));
		}
		ASSERT(MUTEX_NOT_HELD(QLOCK(q)));
		mutex_exit(&stp->sd_lock);

		return (0);

	/*
	 * The following set of cases deal with situations where two stream
	 * heads are connected to each other (twisted streams).  These messages
	 * have no meaning at the stream head.
	 */
	case M_BREAK:
	case M_CTL:
	case M_DELAY:
	case M_START:
	case M_STOP:
	case M_IOCDATA:
	case M_STARTI:
	case M_STOPI:
		freemsg(bp);
		return (0);

	case M_IOCTL:
		mutex_enter(&stp->sd_lock);
		if (stp->sd_struiodnak) {
			/*
			 * Defer NAK to the streamhead.
			 */
			mblk_t *mp = stp->sd_struionak;

			while (mp && mp->b_next)
				mp = mp->b_next;
			if (mp)
				mp->b_next = bp;
			else
				stp->sd_struionak = bp;
			bp->b_next = NULL;
			mutex_exit(&stp->sd_lock);
			return (0);
		}
		mutex_exit(&stp->sd_lock);
		/*
		 * Always NAK this condition
		 * (makes no sense)
		 */
		bp->b_datap->db_type = M_IOCNAK;
		/*
		 * Protect against the driver passing up
		 * messages after it has done a qprocsoff.
		 */
		if (OTHERQ(q)->q_next == NULL)
			freemsg(bp);
		else
			qreply(q, bp);
		return (0);

	default:
#ifdef DEBUG
		cmn_err(CE_WARN,
			"bad message type %x received at stream head\n",
			bp->b_datap->db_type);
#endif
		freemsg(bp);
		return (0);
	}

	/* NOTREACHED */
}

/*
 * Write attempts to break the write request into messages conforming
 * with the minimum and maximum packet sizes set downstream.
 *
 * Write will not block if downstream queue is full and
 * O_NDELAY is set, otherwise it will block waiting for the queue to get room.
 *
 * A write of zero bytes gets packaged into a zero length message and sent
 * downstream like any other message.
 *
 * If buffers of the requested sizes are not available, the write will
 * sleep until the buffers become available.
 *
 * Write (if specified) will supply a write offset in a message if it
 * makes sense. This can be specified by downstream modules as part of
 * a M_SETOPTS message.  Write will not supply the write offset if it
 * cannot supply any data in a buffer.  In other words, write will never
 * send down an empty packet due to a write offset.
 */
/* ARGSUSED2 */
int
strwrite(vp, uiop, crp)
	struct vnode *vp;
	struct uio *uiop;
	cred_t *crp;
{
	register struct stdata *stp;
	register struct queue *wqp;
	register mblk_t *mp;
	long rmin, rmax;
	long iosize;
	char waitflag;
	int tempmode;
	int done = 0;
	int error = 0;
	void *canrw;

	ASSERT(vp->v_stream);
	stp = vp->v_stream;
	canrw = stp->sd_struiowrq;

	if (stp->sd_sidp != NULL && stp->sd_vnode->v_type != VFIFO)
		if (error = straccess(stp, JCWRITE))
			return (error);
	/* Fast check of flags before acquiring the lock */
	if (stp->sd_flag & (STWRERR|STRHUP|STPLEX)) {
		mutex_enter(&stp->sd_lock);
		/* This is for POSIX compatibility */
		if ((stp->sd_flag & (STPLEX|STRHUP)) == STRHUP)
			error = EIO;
		else
			error = strgeterr(stp, STWRERR|STRHUP|STPLEX);
		mutex_exit(&stp->sd_lock);
		if (error != 0) {
			if (!(stp->sd_flag & STPLEX) &&
			    (stp->sd_flag & STRSIGPIPE))
				psignal(ttoproc(curthread), SIGPIPE);
			return (error);
		}
	}
	wqp = stp->sd_wrq;

	/* get these values from them cached in the stream head */
	rmin = stp->sd_qn_minpsz;
	rmax = stp->sd_qn_maxpsz;

	/*
	 * Check the min/max packet size constraints.  If min packet size
	 * is non-zero, the write cannot be split into multiple messages
	 * and still guarantee the size constraints.
	 */
	TRACE_1(TR_FAC_STREAMS_FR, TR_STRWRITE_IN, "strwrite in:q %X", wqp);

	ASSERT((rmax >= 0) || (rmax == INFPSZ));
	if (rmax == 0) {
		return (0);
	}
	if (rmin > 0) {
		run_queues = 1;
		if (uiop->uio_resid < rmin) {
			TRACE_3(TR_FAC_STREAMS_FR, TR_STRWRITE_OUT,
				"strwrite out:q %X out %d error %d",
				wqp, 0, ERANGE);
			return (ERANGE);
		}
		if ((rmax != INFPSZ) && (uiop->uio_resid > rmax)) {
			TRACE_3(TR_FAC_STREAMS_FR, TR_STRWRITE_OUT,
				"strwrite out:q %X out %d error %d",
				wqp, 1, ERANGE);
			return (ERANGE);
		}
	}

	/*
	 * Do until count satisfied or error.
	 */
	waitflag = WRITEWAIT;
	if (stp->sd_flag & OLDNDELAY)
		tempmode = uiop->uio_fmode & ~FNDELAY;
	else
		tempmode = uiop->uio_fmode;

	do {
		mblk_t *amp;	/* auto */

		if (! canrw) {
			mutex_enter(&stp->sd_lock);
			while (!canputnext(wqp)) {

				TRACE_1(TR_FAC_STREAMS_FR, TR_STRWRITE_WAIT,
					"strwrite wait:q %X wait", wqp);
				if ((error = strwaitq(stp, waitflag, (off_t)0,
				    tempmode, &done)) != 0 || done) {
					mutex_exit(&stp->sd_lock);
					if ((vp->v_type == VFIFO) &&
					    (uiop->uio_fmode & FNDELAY) &&
					    (error == EAGAIN))
						error = 0;
					goto out;
				}
				TRACE_1(TR_FAC_STREAMS_FR, TR_STRWRITE_WAKE,
					"strwrite wake:q %X awakes", wqp);
			}
			mutex_exit(&stp->sd_lock);
		}

		/*
		 * Determine the size of the next message to be
		 * packaged.  May have to break write into several
		 * messages based on max packet size.
		 */
		if (rmax == INFPSZ)
			iosize = uiop->uio_resid;
		else	iosize = MIN(uiop->uio_resid, rmax);

		if ((error = strmakemsg((struct strbuf *)NULL, iosize, uiop,
		    stp, canrw ? STRUIO_POSTPONE : 0, &amp)) != 0 || !amp) {
			if ((waitflag & NOINTR) && (error == EAGAIN))
				error = 0;
			goto out;
		}
		mp = amp;

		/*
		 * Put block downstream.
		 */
		if ((uiop->uio_resid == 0) && (stp->sd_flag & STRDELIM))
			mp->b_flag |= MSGDELIM;

		while (canrw) {
			/*
			 * Stream supports rwnext() for the write side.
			 */
			struiod_t uiod;

			uiodup(uiop, &uiod.d_uio, uiod.d_iov,
			    sizeof (uiod.d_iov) / sizeof (*uiod.d_iov));
			uiod.d_uio.uio_offset = 0;
			uiod.d_mp = mp;
			if (! (error = rwnext(wqp, &uiod))) {
				if (! uiod.d_mp) {
					uioskip(uiop, iosize);
					goto skip;
				}
				ASSERT(mp == uiod.d_mp);
				/*
				 * No takers, so fall-back to putnext().
				 */
				if (! (error = struioget(wqp, mp, &uiod))) {
					uioskip(uiop, iosize);
					break;
				}
				goto freeout;
			}
			ASSERT(mp == uiod.d_mp);
			if (error == EINVAL) {
				/*
				 * The stream plumbing must have changed while
				 * we where away, so just turn off rwnext()s.
				 */
				canrw = 0;
				if (! (error = struioget(wqp, mp, &uiod))) {
					uioskip(uiop, iosize);
					break;
				}
				goto freeout;
			} else if (error == EBUSY) {
				/*
				 * Couldn't enter a primeter,
				 * so fall-back to putnext().
				 */
				if (! (error = struioget(wqp, mp, &uiod))) {
					uioskip(uiop, iosize);
					break;
				}
				goto freeout;
			} else if (error != EWOULDBLOCK)
				goto freeout;
			/*
			 * Didn't write it, most likely due to down-stream
			 * flow control, so just wait for a strwakeq() from
			 * the flow controlled module then try another write.
			 */
			mutex_enter(&stp->sd_lock);
			TRACE_1(TR_FAC_STREAMS_FR, TR_STRWRITE_WAIT,
				"strwrite wait:q %X wait", wqp);
			if ((error = strwaitq(stp, waitflag, (off_t)0,
			    tempmode, &done)) != 0 || done) {
				mutex_exit(&stp->sd_lock);
				goto freeout;
			}
			TRACE_1(TR_FAC_STREAMS_FR, TR_STRWRITE_WAKE,
				"strwrite wake:q %X awakes", wqp);
			mutex_exit(&stp->sd_lock);
			run_queues = 1;
		}
		TRACE_2(TR_FAC_STREAMS_FR, TR_STRWRITE_PUT,
			"strwrite put:put %X to q %X", wqp, mp);
		putnext(wqp, mp);
	skip:;
		waitflag |= NOINTR;
		queuerun();
		TRACE_2(TR_FAC_STREAMS_FR, TR_STRWRITE_RESID,
			"strwrite resid:q %X resid %d", wqp, uiop->uio_resid);
	} while (uiop->uio_resid);

out:
	TRACE_3(TR_FAC_STREAMS_FR, TR_STRWRITE_OUT,
		"strwrite out:q %X out %d error %d", wqp, 2, error);
	return (error);
freeout:
	freemsg(mp);
	TRACE_3(TR_FAC_STREAMS_FR, TR_STRWRITE_OUT,
		"strwrite out:q %X out %d error %d", wqp, 3, error);
	return (error);
}

/*
 * Stream head write service routine.
 * Its job is to wake up any sleeping writers when a queue
 * downstream needs data (part of the flow control in putq and getq).
 * It also must wake anyone sleeping on a poll().
 * For stream head right below mux module, it must also invoke put procedure
 * of next downstream module.
 */
int
strwsrv(q)
	register queue_t *q;
{
	register struct stdata *stp;
	register queue_t *tq;
	register qband_t *qbp;
	register int i;
	qband_t *myqbp;
	int isevent;
	unsigned char	qbf[NBAND];	/* band flushing backenable flags */

	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STRWSRV, "strwsrv:q %X", q);
	stp = (struct stdata *)q->q_ptr;
	ASSERT(qclaimed(q));
	mutex_enter(&stp->sd_lock);
	ASSERT(!(stp->sd_flag & STPLEX));

	if (stp->sd_flag & WSLEEP) {
		stp->sd_flag &= ~WSLEEP;
		cv_broadcast(&q->q_wait);
	}
	mutex_exit(&stp->sd_lock);

	/* The other end of a stream pipe went away. */
	if ((tq = q->q_next) == NULL) {
		return (0);
	}

	/* Find the next module forward that has a service procedure */
	tq = q->q_nfsrv;
	ASSERT(tq != NULL);

	if ((q->q_flag & QBACK)) {
		if ((tq->q_flag & QFULL)) {
			mutex_enter(QLOCK(tq));
			if (!(tq->q_flag & QFULL)) {
				mutex_exit(QLOCK(tq));
				goto wakeup;
			}
			/*
			 * The queue must have become full again. Set QWANTW
			 * again so strwsrv will be back enabled when
			 * the queue becomes non-full next time.
			 */
			tq->q_flag |= QWANTW;
			mutex_exit(QLOCK(tq));
		} else {
		wakeup:
			mutex_enter(&stp->sd_lock);
			if (stp->sd_sigflags & S_WRNORM)
				strsendsig(stp->sd_siglist, S_WRNORM, 0L);
			mutex_exit(&stp->sd_lock);
			pollwakeup_safe(&stp->sd_pollist, POLLWRNORM);
		}
	}

	isevent = 0;
	i = 1;
	bzero((caddr_t)qbf, NBAND);
	mutex_enter(QLOCK(tq));
	if ((myqbp = q->q_bandp) != NULL)
		for (qbp = tq->q_bandp; qbp && myqbp; qbp = qbp->qb_next) {
			ASSERT(myqbp);
			if ((myqbp->qb_flag & QB_BACK)) {
				if (qbp->qb_flag & QB_FULL) {
					/*
					 * The band must have become full again.
					 * Set QB_WANTW again so strwsrv will
					 * be back enabled when the band becomes
					 * non-full next time.
					 */
					qbp->qb_flag |= QB_WANTW;
				} else {
					isevent = 1;
					qbf[i] = 1;
				}
			}
			myqbp = myqbp->qb_next;
			i++;
		}
	mutex_exit(QLOCK(tq));

	if (isevent) {
	    for (i = tq->q_nband; i; i--) {
		if (qbf[i]) {
			mutex_enter(&stp->sd_lock);
			if (stp->sd_sigflags & S_WRBAND)
				strsendsig(stp->sd_siglist, S_WRBAND, (long)i);
			mutex_exit(&stp->sd_lock);
			pollwakeup_safe(&stp->sd_pollist, POLLWRBAND);
		}
	    }
	}

	return (0);
}

/*
 * XXX - The kbd and ms ioctls have been
 * hardcoded in the stream head.  This should
 * be fixed later on.
 */
#define	FORNOW
#ifdef FORNOW
#include <sys/kbio.h>
#include <sys/msio.h>
#include <sys/tty.h>
#include <sys/ptyvar.h>
#include <sys/vuid_event.h>
#endif
#ifdef LOCKNEST
#include <sys/fs/fifonode.h>
#endif

/*
 * ioctl for streams
 */
int
strioctl(
	struct vnode *vp,
	int cmd,
	int arg,
	int flag,
	int copyflag,
	cred_t *crp,
	int *rvalp
)
{
	register struct stdata *stp;
	struct strioctl strioc;
	struct uio uio;
	struct iovec iov;
	enum jcaccess access;
	mblk_t *mp;
#ifdef _VPIX
	mblk_t *bp1 = NULL;
	struct v86blk *p_v86blk;
	proc_t *p;
#endif
	int error = 0;
	int done = 0;
	char *fmt = NULL;
	long	rmin, rmax;
	o_pid_t *oldttyp;
	register queue_t *wrq;
	register queue_t *rdq;

	if (flag & FKIOCTL)
		copyflag = K_TO_K;
	ASSERT(vp->v_stream);
	ASSERT(copyflag == U_TO_K || copyflag == K_TO_K);
	stp = vp->v_stream;

	TRACE_3(TR_FAC_STREAMS_FR,
		TR_IOCTL_ENTER,
		"strioctl:stp %X cmd %X arg %X", stp, cmd, arg);

#ifdef C2_AUDIT
	if (audit_active)
		audit_strioctl(vp, cmd, arg, flag, copyflag, crp, rvalp);
#endif

	/*
	 *  if a message is being "held" awaiting possible consolidation,
	 *  send it downstream before processing ioctl.
	 */
	wrq = stp->sd_wrq;
	rdq = RD(wrq);

	switch (cmd) {
	case I_RECVFD:
	case I_E_RECVFD:
		access = JCREAD;
		break;

	case I_FDINSERT:
	case I_SENDFD:
	case TIOCSTI:
		access = JCWRITE;
		break;

	case TCGETA:
	case TCGETS:
	case TIOCGETP:
	case TIOCGPGRP:
	case JWINSIZE:
	case TIOCGSID:
	case TIOCMGET:
	case LDGETT:
	case TIOCGETC:
	case TIOCLGET:
	case TIOCGLTC:
	case TIOCGETD:
	case TIOCGWINSZ:
	case LDGMAP:
	case I_CANPUT:
	case I_NREAD:
	case FIONREAD:
	case FIORDCHK:
	case I_FIND:
	case I_LOOK:
	case I_GRDOPT:
	case I_GETEV:
	case I_GETSIG:
	case I_PEEK:
	case I_GWROPT:
	case I_LIST:
	case I_CKBAND:
	case I_GETBAND:
	case I_GETCLTIME:
		access = JCGETP;
		break;

	/* We should never see these here, should be handled by iwscn */
	case SRIOCSREDIR:
	case SRIOCISREDIR:
		return (EINVAL);

	default:
		access = JCSETP;
		break;
	}


	if (stp->sd_sidp != NULL && stp->sd_vnode->v_type != VFIFO)
		if (error = straccess(stp, access))
			return (error);
	mutex_enter(&stp->sd_lock);

	switch (cmd) {
	case I_RECVFD:
	case I_E_RECVFD:
	case I_PEEK:
	case I_NREAD:
	case FIONREAD:
	case FIORDCHK:
	case I_ATMARK:
	case FIONBIO:
	case FIOASYNC:
		if (stp->sd_flag & (STRDERR|STPLEX)) {
			error = strgeterr(stp, STRDERR|STPLEX);
			mutex_exit(&stp->sd_lock);
			return (error);
		}
		break;

	default:
		if (stp->sd_flag & (STRDERR|STWRERR|STPLEX)) {
			error = strgeterr(stp, STRDERR|STWRERR|STPLEX);
			mutex_exit(&stp->sd_lock);
			return (error);
		}
	}

	mutex_exit(&stp->sd_lock);

	switch (cmd) {

	default:
		if (((cmd & IOCTYPE) == LDIOC) ||
		    ((cmd & IOCTYPE) == tIOC) ||
#ifndef FORNOW
		    ((cmd & IOCTYPE) == TIOC)) {
#else
		    ((cmd & IOCTYPE) == TIOC) ||
		    ((cmd & IOCTYPE) == KIOC) ||
		    ((cmd & IOCTYPE) == MSIOC) ||
		    ((cmd & IOCTYPE) == VUIOC)) {
#endif

			/*
			 * The ioctl is a tty ioctl - set up strioc buffer
			 * and call strdoioctl() to do the work.
			 */
			if (stp->sd_flag & STRHUP)
				return (ENXIO);
			strioc.ic_cmd = cmd;
			strioc.ic_timout = INFTIM;

			switch (cmd) {
#ifdef FORNOW
			case TIOCLBIS:  /* ttcompat */
			case TIOCLBIC:  /* ttcompat */
			case TIOCLSET:  /* ttcompat */
				strioc.ic_len = TRANSPARENT;
				strioc.ic_dp = (char *)&arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, STRINT, crp, rvalp));
#endif

			case TCXONC:
			case TCSBRK:
			case TCFLSH:
			case TCDSET:
				strioc.ic_len = sizeof (int);
				strioc.ic_dp = (char *)&arg;
				return (strdoioctl(stp, &strioc, NULL, K_TO_K,
					STRINT, crp, rvalp));

			case TCSETA:
			case TCSETAW:
			case TCSETAF:
				strioc.ic_len = sizeof (struct termio);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, STRTERMIO, crp, rvalp));

			case TCSETS:
			case TCSETSW:
			case TCSETSF:
				strioc.ic_len = sizeof (struct termios);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, STRTERMIOS, crp, rvalp));

			case LDSETT:
				strioc.ic_len = sizeof (struct termcb);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, STRTERMCB, crp, rvalp));

			case TIOCSETP:
				strioc.ic_len = sizeof (struct sgttyb);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, STRSGTTYB, crp, rvalp));

			case TIOCSTI:
				if (crp->cr_uid) {
					if ((flag & FREAD) == 0)
						return (EPERM);
					if (stp->sd_sidp !=
					    ttoproc(curthread)->p_sessp->s_sidp)
						return (EACCES);
				}
				strioc.ic_len = sizeof (char);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, "c", crp, rvalp));
#ifdef FORNOW
			case TIOCLGET:  /* ttcompat */
				strioc.ic_len = TRANSPARENT;
				strioc.ic_dp = (char *)&arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, STRINT, crp, rvalp));
#endif


			case TCGETA:
				fmt = STRTERMIO;
				/* FALLTHRU */
			case TCGETS:
				if (fmt == NULL)
					fmt = STRTERMIOS;
				/* FALLTHRU */
			case LDGETT:
				if (fmt == NULL)
					fmt = STRTERMCB;
				/* FALLTHRU */
			case TIOCGETP:
				if (fmt == NULL)
					fmt = STRSGTTYB;
				strioc.ic_len = 0;
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, fmt, crp, rvalp));

#ifdef FORNOW
			/*
			 * XXX - The kbd and ms ioctls have been
			 * hardcoded in the stream head.  This should
			 * be fixed later on.
			 */
			case TIOCSWINSZ:
				strioc.ic_len = sizeof (struct winsize);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case TIOCSSIZE:
				strioc.ic_len = sizeof (struct ttysize);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case TIOCSSOFTCAR:
			case KIOCTRANS:
			case KIOCTRANSABLE:
			case KIOCCMD:
			case KIOCSDIRECT:
			case KIOCSCOMPAT:
			case VUIDSFORMAT:
				strioc.ic_len = sizeof (int);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case KIOCSETKEY:
			case KIOCGETKEY:
				strioc.ic_len = sizeof (struct kiockey);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case KIOCSKEY:
			case KIOCGKEY:
				strioc.ic_len = sizeof (struct kiockeymap);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case KIOCSLED:
				strioc.ic_len = sizeof (char);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case MSIOSETPARMS:
				strioc.ic_len = sizeof (Ms_parms);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case VUIDSADDR:
			case VUIDGADDR:
				strioc.ic_len = sizeof (struct vuid_addr_probe);
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
					copyflag, (char *)NULL, crp, rvalp));

			case TIOCGSOFTCAR:
			case TIOCGWINSZ:
			case TIOCGSIZE:
			case KIOCGTRANS:
			case KIOCGTRANSABLE:
			case KIOCTYPE:
			case KIOCGDIRECT:
			case KIOCGCOMPAT:
			case KIOCLAYOUT:
			case KIOCGLED:
			case MSIOGETPARMS:
			case VUIDGFORMAT:
				strioc.ic_len = 0;
				strioc.ic_dp = (char *)arg;
				return (strdoioctl(stp, &strioc, NULL,
				copyflag, (char *)NULL, crp, rvalp));
#endif
			}
		}

#ifdef _VPIX
		/*
		 *  Support for VP/IX ioctls
		 */

		switch (cmd) {
		case KDSKBMODE:
		case AIOCDOSMODE:
		case AIOCINTTYPE:
		case AIOCNONDOSMODE:
		case AIOCINFO:
			while (!(bp1 = allocb(max(sizeof (struct v86blk),
			    sizeof (struct copyreq)), BPRI_HI))) {
				if (error = strwaitbuf(sizeof (struct v86blk),
				    BPRI_HI)) {
					return (error);
				}
			}
			p = ttoproc(curthread);
			p_v86blk = (struct v86blk *)bp1->b_wptr;
			p_v86blk->v86b_t = curthread;
			p_v86blk->v86b_p_pid = p->p_pidp->pid_id;
			p_v86blk->v86b_p_v86 = (v86_t *)curthread->t_v86data;
			bp1->b_wptr += sizeof (struct v86blk);
		}
#endif

		/*
		 * Unknown cmd - send down request to support
		 * transparent ioctls.
		 */

		strioc.ic_cmd = cmd;
		strioc.ic_timout = INFTIM;
		strioc.ic_len = TRANSPARENT;
		strioc.ic_dp = (char *)&arg;
#ifdef _VPIX
		return (strdoioctl(stp, &strioc, bp1, copyflag, (char *)NULL,
		    crp, rvalp));
#else
		return (strdoioctl(stp, &strioc, NULL, copyflag, (char *)NULL,
		    crp, rvalp));
#endif

	case I_STR:
		/*
		 * Stream ioctl.  Read in an strioctl buffer from the user
		 * along with any data specified and send it downstream.
		 * Strdoioctl will wait allow only one ioctl message at
		 * a time, and waits for the acknowledgement.
		 */

		if (stp->sd_flag & STRHUP)
			return (ENXIO);
		error = strcopyin((caddr_t)arg, (caddr_t)&strioc,
		    sizeof (struct strioctl), STRIOCTL, copyflag);
		if (error)
			return (error);
		if ((strioc.ic_len < 0) || (strioc.ic_timout < -1))
			return (EINVAL);
		error = strdoioctl(stp, &strioc, NULL, copyflag, (char *)NULL,
		    crp, rvalp);
		if (error == 0)
			error = strcopyout((caddr_t)&strioc, (caddr_t)arg,
			    sizeof (struct strioctl), STRIOCTL, copyflag);
		return (error);

	case I_NREAD:
		/*
		 * Return number of bytes of data in first message
		 * in queue in "arg" and return the number of messages
		 * in queue in return value.
		 */
	    {
		int size = 0;
		int count = 0;

		mutex_enter(QLOCK(rdq));
		if ((mp = rdq->q_first) != NULL) {
			while (mp) {
				if (!(mp->b_flag & MSGNOGET))
					break;
				mp = mp->b_next;
			}
			if (mp)
				size = msgdsize(mp);
		}
		mutex_exit(QLOCK(rdq));
		if (stp->sd_struiordq) {
			infod_t infod;

			infod.d_cmd = INFOD_COUNT;
			infod.d_count = 0;
			if (mp == NULL) {
				infod.d_cmd |= INFOD_FIRSTBYTES;
				infod.d_bytes = 0;
			}
			infod.d_res = 0;
			infonext(rdq, &infod);
			count = infod.d_count;
			if (infod.d_res & INFOD_FIRSTBYTES)
				size = infod.d_bytes;
		}
		error = strcopyout((caddr_t)&size, (caddr_t)arg,
				sizeof (size), STRINT, copyflag);
		if (!error) {
			if (mp) {
				for (; mp; mp = mp->b_next)
					if (!(mp->b_flag & MSGNOGET))
						count++;
			}
			*rvalp = count;
		}
		return (error);
	    }

	case FIONREAD:
		/*
		 * Return number of bytes of data in all data messages
		 * in queue in "arg".
		 */
	    {
		int size = 0;

		mutex_enter(QLOCK(rdq));
		if ((mp = rdq->q_first) != NULL) {
			while (mp) {
				if ((mp->b_flag & MSGNOGET) == 0)
					size += msgdsize(mp);
				mp = mp->b_next;
			}
		}
		mutex_exit(QLOCK(rdq));
		if (stp->sd_struiordq) {
			infod_t infod;

			infod.d_cmd = INFOD_BYTES;
			infod.d_res = 0;
			infod.d_bytes = 0;
			infonext(rdq, &infod);
			size += infod.d_bytes;
		}
		error = strcopyout((caddr_t)&size, (caddr_t)arg,
				sizeof (size), STRINT, copyflag);

		*rvalp = 0;
		return (error);
	    }
	case FIORDCHK:
		/*
		 * FIORDCHK does not use arg value (like FIONREAD),
		 * instead a count is returned. I_NREAD value may
		 * not be accurate but safe. The real thing to do is
		 * to add the msgdsizes of all data  messages until
		 * a non-data message.
		 */
	    {
		int size = 0;

		mutex_enter(QLOCK(rdq));
		if ((mp = rdq->q_first) != NULL) {
			while (mp) {
				if ((mp->b_flag & MSGNOGET) == 0)
					size += msgdsize(mp);
				mp = mp->b_next;
			}
		}
		mutex_exit(QLOCK(rdq));
		if (stp->sd_struiordq) {
			infod_t infod;

			infod.d_cmd = INFOD_BYTES;
			infod.d_res = 0;
			infod.d_bytes = 0;
			infonext(rdq, &infod);
			size += infod.d_bytes;
		}

		*rvalp = size;
		return (0);
	    }

	case I_FIND:
		/*
		 * Get module name.
		 */
	    {
		char mname[FMNAMESZ+2];
		queue_t *q;
		int i;

		error = strcopyin((caddr_t)arg, mname, FMNAMESZ+1, STRNAME,
		    copyflag);
		if (error)
			return (error);
		/*
		 * Protect the system from a name too long.
		 */
		mname[FMNAMESZ+1] = '\0';
		if (strlen(mname) > FMNAMESZ)
			return (EINVAL);

		/*
		 * Find module in fmodsw.
		 * XXX - What purpose does this serve?
		 */
		if ((i = findmod(mname)) < 0) {
			TRACE_0(TR_FAC_STREAMS_FR,
				TR_I_CANT_FIND, "couldn't I_FIND");
			return (EINVAL);
		}
		(void) fmod_unlock(i);

		*rvalp = 0;

		/* Look downstream to see if module is there. */
		claimstr(stp->sd_wrq);
		for (q = stp->sd_wrq->q_next; q; q = q->q_next) {
			if (q->q_flag&QREADR) {
				q = NULL;
				break;
			}
			if (strcmp(mname, q->q_qinfo->qi_minfo->mi_idname) == 0)
				break;
		}
		releasestr(stp->sd_wrq);

		*rvalp =  (q ? 1 : 0);
		return (error);
	    }

	case I_PUSH:
		/*
		 * Push a module.
		 */

	    {
		char mname[FMNAMESZ+2];
		int i;
		dev_t dummydev;

		if (stp->sd_flag & STRHUP)
			return (ENXIO);
		mutex_enter(&stp->sd_lock);
		if (stp->sd_pushcnt >= nstrpush) {
			mutex_exit(&stp->sd_lock);
			return (EINVAL);
		}
		stp->sd_pushcnt++;
		mutex_exit(&stp->sd_lock);

		/*
		 * Get module name and look up in fmodsw.
		 */
		error = strcopyin((caddr_t)arg, mname, FMNAMESZ+1, STRNAME,
		    copyflag);
		if (error)
			goto push_failed;
		/*
		 * Protect the system from a name too long.
		 */
		mname[FMNAMESZ+1] = '\0';
		if (strlen(mname) > FMNAMESZ) {
			error = EINVAL;
			goto push_failed;
		}

		/*
		 * Note: fmod lock is released after module is popped.
		 */

		if ((i = findmod(mname)) < 0) {
			error = EINVAL;
			goto push_failed;
		}

		TRACE_2(TR_FAC_STREAMS_FR,
			TR_I_PUSH,
			"I_PUSH:mod %d to %X", i, stp);

		if (error = strsyncplumb(stp, flag, cmd)) {
			(void) fmod_unlock(i);
			goto push_failed;
		}
		/*
		 * Push new module and call its open routine
		 * via qattach().  Modules don't change device
		 * numbers, so just ignore dummydev here.
		 */
		dummydev = vp->v_rdev;
		oldttyp = u.u_ttyp;	/* used only by old tty drivers */
		if ((error = qattach(rdq, &dummydev, 0,
		    FMODSW, i, crp)) == 0) {
			if (vp->v_type == VCHR) { /* sorry, no pipes allowed */
				if (oldttyp == NULL && u.u_ttyp != NULL) {

					/*
					 * pre SVR4 driver has allocated the
					 * stream as a controlling terminal -
					 * check against SVR4 criteria and
					 * deallocate it if it fails
					 */
					if (!strctty(ttoproc(curthread), stp)) {
						*u.u_ttyp = 0;
						u.u_ttyp = NULL;
					}
				} else if (stp->sd_flag & STRISTTY) {

					/*
					 * this is a post SVR4 tty driver -
					 * try to allocate it as a
					 * controlling terminal
					 */
					(void) strctty(ttoproc(curthread), stp);
				}
			}
		} else {
			(void) fmod_unlock(i);
			mutex_enter(&stp->sd_lock);
			stp->sd_pushcnt--;
			mutex_exit(&stp->sd_lock);
		}
		/*
		 * If flow control is on, don't break it - enable
		 * first back queue with svc procedure.
		 */
		if (rdq->q_flag & QWANTW) {
			/* Note: no setqback here - use pri -1. */
			backenable(RD(wrq->q_next), -1);
		}

		mutex_enter(&stp->sd_lock);
		stp->sd_flag &= ~STRPLUMB;

		/*
		 * As a performance concern we are caching the values of
		 * q_minpsz and q_maxpsz of the module below the stream
		 * head in the stream head.
		 */
		mutex_enter(QLOCK(stp->sd_wrq->q_next));
		rmin = stp->sd_wrq->q_next->q_minpsz;
		rmax = stp->sd_wrq->q_next->q_maxpsz;
		mutex_exit(QLOCK(stp->sd_wrq->q_next));

		/* Do this processing here as a performance concern */
		if (strmsgsz != 0) {
			if (rmax == INFPSZ)
				rmax = strmsgsz;
			else  {
				if (vp->v_type == VFIFO)
					rmax = MIN(PIPE_BUF, rmax);
				else	rmax = MIN(strmsgsz, rmax);
			}
		}

		mutex_enter(QLOCK(wrq));
		stp->sd_qn_minpsz = rmin;
		stp->sd_qn_maxpsz = rmax;
		mutex_exit(QLOCK(wrq));

		cv_broadcast(&stp->sd_monitor);
		mutex_exit(&stp->sd_lock);
		return (error);

		push_failed:
		mutex_enter(&stp->sd_lock);
		stp->sd_pushcnt--;
		mutex_exit(&stp->sd_lock);
		return (error);
	    }

	case I_POP:
	    {
		queue_t	*q;
		queue_t	*rmq;
		int	error;
		int	status;
		int	mid;

		if (stp->sd_flag & STRHUP)
			return (ENXIO);
		if (!wrq->q_next)	/* for broken pipes */
			return (EINVAL);

		if (status = strsyncplumb(stp, flag, cmd))
			return (status);

		q = wrq->q_next;
		TRACE_2(TR_FAC_STREAMS_FR,
			TR_I_POP,
			"I_POP:%X from %X", q, stp);
		if (q->q_next && !(q->q_flag & QREADR)) {
			rmq = wrq->q_next;
			if (!(rmq->q_flag & QISDRV)) {
				mutex_enter(&fmodsw_lock);
				mid = findmodbyname(
					rmq->q_qinfo->qi_minfo->mi_idname);
				mutex_exit(&fmodsw_lock);
				qdetach(RD(rmq), 1, flag, crp);
				if (mid != -1) {
					register struct fmodsw *fmp;

					fmp = &fmodsw[mid];
					ASSERT(fmp);
					rw_exit(fmp->f_lock);
				}
			} else {
				ASSERT(!SAMESTR(rmq));
				qdetach(RD(rmq), 1, flag, crp);
			}
			mutex_enter(&stp->sd_lock);
			cv_broadcast(&stp->sd_monitor);
			stp->sd_pushcnt--;
			mutex_exit(&stp->sd_lock);
			error = 0;
		} else
			error = EINVAL;
		mutex_enter(&stp->sd_lock);
		stp->sd_flag &= ~STRPLUMB;

		/*
		 * As a performance concern we are caching the values of
		 * q_minpsz and q_maxpsz of the module below the stream
		 * head in the stream head.
		 */
		mutex_enter(QLOCK(wrq->q_next));
		rmin = wrq->q_next->q_minpsz;
		rmax = wrq->q_next->q_maxpsz;
		mutex_exit(QLOCK(wrq->q_next));

		/* Do this processing here as a performance concern */
		if (strmsgsz != 0) {
			if (rmax == INFPSZ)
				rmax = strmsgsz;
			else  {
				if (vp->v_type == VFIFO)
					rmax = MIN(PIPE_BUF, rmax);
				else	rmax = MIN(strmsgsz, rmax);
			}
		}

		mutex_enter(QLOCK(wrq));
		stp->sd_qn_minpsz = rmin;
		stp->sd_qn_maxpsz = rmax;
		mutex_exit(QLOCK(wrq));

		cv_broadcast(&stp->sd_monitor);
		mutex_exit(&stp->sd_lock);
		return (error);
	    }

	case I_LOOK:
		/*
		 * Get name of first module downstream.
		 * If no module, return an error.
		 */
	    {
		claimstr(wrq);
		if (SAMESTR(wrq) && wrq->q_next->q_next) {
			error = strcopyout(
			    wrq->q_next->q_qinfo->qi_minfo->mi_idname,
			    (char *)arg, FMNAMESZ+1, STRNAME, copyflag);
			releasestr(wrq);
			return (error);
		}
		releasestr(wrq);
		return (EINVAL);
	    }

	case I_LINK:
	case I_PLINK:
		/*
		 * Link a multiplexor.
		 */
		return (mlink(vp, cmd, arg, crp, rvalp));

	case I_UNLINK:
	case I_PUNLINK:
		/*
		 * Unlink a multiplexor.
		 * If arg is -1, unlink all links for which this is the
		 * controlling stream.  Otherwise, arg is an index number
		 * for a link to be removed.
		 */
	    {
		struct linkinfo *linkp;
		int type;

		TRACE_1(TR_FAC_STREAMS_FR,
			TR_I_UNLINK, "I_UNLINK/I_PUNLINK:%X", stp);
		if (vp->v_type == VFIFO) {
			return (EINVAL);
		}
		if (cmd == I_UNLINK)
			type = LINKIOCTL|LINKNORMAL;
		else	/* I_PUNLINK */
			type = LINKIOCTL|LINKPERSIST;
		if (arg == 0) {
			return (EINVAL);
		}
		if (arg == MUXID_ALL)
			error = munlinkall(stp, type, crp, rvalp);
		else {
			mutex_enter(&muxifier);
			if (!(linkp = findlinks(stp, arg, type))) {
				/* invalid user supplied index number */
				mutex_exit(&muxifier);
				return (EINVAL);
			}
			/* munlink drops the muxifier lock */
			error = munlink(stp, linkp, type, crp, rvalp);
		}
		return (error);
	    }

	case I_FLUSH:
		/*
		 * send a flush message downstream
		 * flush message can indicate
		 * FLUSHR - flush read queue
		 * FLUSHW - flush write queue
		 * FLUSHRW - flush read/write queue
		 */
		if (stp->sd_flag & STRHUP)
			return (ENXIO);
		if (arg & ~FLUSHRW)
			return (EINVAL);
		/*CONSTCOND*/
		while (1) {
			if (putnextctl1(stp->sd_wrq, M_FLUSH, arg)) {
				break;
			}
			if (error = strwaitbuf(1, BPRI_HI)) {
				return (error);
			}
		}

		/*
		 * Send down an unsupported ioctl and wait for the nack
		 * in order to allow the M_FLUSH to propagate back
		 * up to the stream head.
		 * Replaces if (qready()) runqueues();
		 */
		strioc.ic_cmd = -1;	/* The unsupported ioctl */
		strioc.ic_timout = 0;
		strioc.ic_len = 0;
		strioc.ic_dp = (char *)NULL;
		(void) strdoioctl(stp, &strioc, NULL, K_TO_K,
				"dummy", crp, rvalp);
		*rvalp = 0;
		return (0);

	case I_FLUSHBAND:
	    {
		struct bandinfo binfo;

		error = strcopyin((caddr_t)arg, (caddr_t)&binfo,
		    sizeof (struct bandinfo), STRBANDINFO, copyflag);
		if (error)
			return (error);
		if (stp->sd_flag & STRHUP)
			return (ENXIO);
		if (binfo.bi_flag & ~FLUSHRW)
			return (EINVAL);
		while (!(mp = allocb(2, BPRI_HI))) {
			if (error = strwaitbuf(2, BPRI_HI))
				return (error);
		}
		mp->b_datap->db_type = M_FLUSH;
		*mp->b_wptr++ = binfo.bi_flag | FLUSHBAND;
		*mp->b_wptr++ = binfo.bi_pri;
		putnext(stp->sd_wrq, mp);
		/*
		 * Send down an unsupported ioctl and wait for the nack
		 * in order to allow the M_FLUSH to propagate back
		 * up to the stream head.
		 * Replaces if (qready()) runqueues();
		 */
		strioc.ic_cmd = -1;	/* The unsupported ioctl */
		strioc.ic_timout = 0;
		strioc.ic_len = 0;
		strioc.ic_dp = (char *)NULL;
		(void) strdoioctl(stp, &strioc, NULL, K_TO_K,
				"dummy", crp, rvalp);
		*rvalp = 0;
		return (0);
	    }

	case I_SRDOPT:
		/*
		 * Set read options
		 *
		 * RNORM - default stream mode
		 * RMSGN - message no discard
		 * RMSGD - message discard
		 * RPROTNORM - fail read with EBADMSG for M_[PC]PROTOs
		 * RPROTDAT - convert M_[PC]PROTOs to M_DATAs
		 * RPROTDIS - discard M_[PC]PROTOs and retain M_DATAs
		 */
		if (arg & ~(RMODEMASK | RPROTMASK))
			return (EINVAL);

		mutex_enter(&stp->sd_lock);
		switch (arg & RMODEMASK) {
		case RNORM:
			stp->sd_flag &= ~(RMSGDIS | RMSGNODIS);
			break;
		case RMSGD:
			stp->sd_flag = (stp->sd_flag & ~RMSGNODIS) | RMSGDIS;
			break;
		case RMSGN:
			stp->sd_flag = (stp->sd_flag & ~RMSGDIS) | RMSGNODIS;
			break;
		}

		switch (arg & RPROTMASK) {
		case RPROTNORM:
			stp->sd_flag &= ~(RDPROTDAT | RDPROTDIS);
			break;

		case RPROTDAT:
			stp->sd_flag = ((stp->sd_flag & ~RDPROTDIS) |
			    RDPROTDAT);
			break;

		case RPROTDIS:
			stp->sd_flag = ((stp->sd_flag & ~RDPROTDAT) |
			    RDPROTDIS);
			break;
		}
		mutex_exit(&stp->sd_lock);
		return (0);

	case I_GRDOPT:
		/*
		 * Get read option and return the value
		 * to spot pointed to by arg
		 */
	    {
		int rdopt;

		rdopt = ((stp->sd_flag & RMSGDIS) ? RMSGD :
			((stp->sd_flag & RMSGNODIS) ? RMSGN : RNORM));
		rdopt |= ((stp->sd_flag & RDPROTDAT) ? RPROTDAT :
			((stp->sd_flag & RDPROTDIS) ? RPROTDIS : RPROTNORM));

		error = strcopyout((caddr_t)&rdopt, (caddr_t)arg,
				sizeof (rdopt), STRINT, copyflag);
		return (error);
	    }

	case I_SERROPT:
		/*
		 * Set error options
		 *
		 * RERRNORM - persistent read errors
		 * RERRNONPERSIST - non-persistent read errors
		 * WERRNORM - persistent write errors
		 * WERRNONPERSIST - non-persistent write errors
		 */
		if (arg & ~(RERRMASK | WERRMASK))
			return (EINVAL);

		mutex_enter(&stp->sd_lock);
		switch (arg & RERRMASK) {
		case RERRNORM:
			stp->sd_flag &= ~STRDERRNONPERSIST;
			break;
		case RERRNONPERSIST:
			stp->sd_flag |= STRDERRNONPERSIST;
			break;
		}
		switch (arg & WERRMASK) {
		case WERRNORM:
			stp->sd_flag &= ~STWRERRNONPERSIST;
			break;
		case WERRNONPERSIST:
			stp->sd_flag |= STWRERRNONPERSIST;
			break;
		}
		mutex_exit(&stp->sd_lock);
		return (0);

	case I_GERROPT:
		/*
		 * Get error option and return the value
		 * to spot pointed to by arg
		 */
	    {
		int erropt = 0;

		erropt |= (stp->sd_flag & STRDERRNONPERSIST) ? RERRNONPERSIST :
			RERRNORM;
		erropt |= (stp->sd_flag & STWRERRNONPERSIST) ? WERRNONPERSIST :
			WERRNORM;
		error = strcopyout((caddr_t)&erropt, (caddr_t)arg,
				sizeof (erropt), STRINT, copyflag);
		return (error);
	    }

	case I_SETSIG:
		/*
		 * Register the calling proc to receive the SIGPOLL
		 * signal based on the events given in arg.  If
		 * arg is zero, remove the proc from register list.
		 */
	    {
		strsig_t *ssp, *pssp;
		struct pid *pidp;

		pssp = NULL;
		pidp = curproc->p_pidp;
		mutex_enter(&stp->sd_lock);
		for (ssp = stp->sd_siglist; ssp && (ssp->ss_pidp != pidp);
			pssp = ssp, ssp = ssp->ss_next)
			;

		if (arg) {
			if (arg & ~(S_INPUT|S_HIPRI|S_MSG|S_HANGUP|S_ERROR|
			    S_RDNORM|S_WRNORM|S_RDBAND|S_WRBAND|S_BANDURG)) {
				mutex_exit(&stp->sd_lock);
				return (EINVAL);
			}
			if ((arg & S_BANDURG) && !(arg & S_RDBAND)) {
				mutex_exit(&stp->sd_lock);
				return (EINVAL);
			}

			/*
			 * If proc not already registered, add it
			 * to list.
			 */
			if (!ssp) {
				ssp = kmem_cache_alloc(strsig_cache, KM_SLEEP);
				ssp->ss_next = NULL;
				if (pssp)
					pssp->ss_next = ssp;
				else
					stp->sd_siglist = ssp;
				mutex_enter(&pidlock);
				PID_HOLD(pidp);
				mutex_exit(&pidlock);
				ssp->ss_pidp = pidp;
			}

			/*
			 * Set events.
			 */
			ssp->ss_events = arg;
		} else {
			/*
			 * Remove proc from register list.
			 */
			if (ssp) {
				mutex_enter(&pidlock);
				PID_RELE(pidp);
				mutex_exit(&pidlock);
				if (pssp)
					pssp->ss_next = ssp->ss_next;
				else
					stp->sd_siglist = ssp->ss_next;
				kmem_cache_free(strsig_cache, ssp);
			} else {
				mutex_exit(&stp->sd_lock);
				return (EINVAL);
			}
		}

		/*
		 * Recalculate OR of sig events.
		 */
		stp->sd_sigflags = 0;
		for (ssp = stp->sd_siglist; ssp; ssp = ssp->ss_next)
			stp->sd_sigflags |= ssp->ss_events;
		mutex_exit(&stp->sd_lock);
		return (0);
	    }

	case I_GETSIG:
		/*
		 * Return (in arg) the current registration of events
		 * for which the calling proc is to be signalled.
		 */
	    {
		strsig_t *ssp;
		struct pid  *pidp;

		pidp = curproc->p_pidp;
		mutex_enter(&stp->sd_lock);
		for (ssp = stp->sd_siglist; ssp; ssp = ssp->ss_next)
			if (ssp->ss_pidp == pidp) {
				error = strcopyout((caddr_t)&ssp->ss_events,
				    (caddr_t)arg, sizeof (int), STRINT,
				    copyflag);
				mutex_exit(&stp->sd_lock);
				return (error);
			}
		mutex_exit(&stp->sd_lock);
		return (EINVAL);
	    }

	case I_PEEK:
	    {
		struct strpeek strpeek;
		int n;
		mblk_t *fmp;

		error = strcopyin((caddr_t)arg, (caddr_t)&strpeek,
		    sizeof (strpeek), STRPEEK, copyflag);
		if (error)
			return (error);

		mutex_enter(QLOCK(rdq));
		mp = rdq->q_first;
		while (mp) {
			if (!(mp->b_flag & MSGNOGET))
				break;
			mp = mp->b_next;
		}
		if (mp) {
			if ((strpeek.flags & RS_HIPRI) &&
			    queclass(mp) == QNORM) {
				*rvalp = 0;
				mutex_exit(QLOCK(rdq));
				return (0);
			}
		} else if (stp->sd_struiordq == NULL ||
		    (strpeek.flags & RS_HIPRI)) {
			/*
			 * No mblks to look at at the streamhead, and this
			 * isn't a synch stream or caller wants high priority
			 * messages but a sync stream only supports QNORM.
			 */
			*rvalp = 0;
			mutex_exit(QLOCK(rdq));
			return (0);
		}
		fmp = mp;

		if (mp && mp->b_datap->db_type == M_PASSFP) {
			mutex_exit(QLOCK(rdq));
			return (EBADMSG);
		}

		if (mp && mp->b_datap->db_type == M_PCPROTO)
			strpeek.flags = RS_HIPRI;
		else
			strpeek.flags = 0;

		/*
		 * First process PROTO blocks, if any.
		 */
		iov.iov_base = strpeek.ctlbuf.buf;
		iov.iov_len = strpeek.ctlbuf.maxlen;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_loffset = 0;
		uio.uio_segflg = (copyflag == U_TO_K) ? UIO_USERSPACE :
		    UIO_SYSSPACE;
		uio.uio_fmode = 0;
		uio.uio_resid = iov.iov_len;
		while (mp && mp->b_datap->db_type != M_DATA &&
		    uio.uio_resid >= 0) {
			if ((n = MIN(uio.uio_resid,
				mp->b_wptr - mp->b_rptr)) != 0 &&
				(error = uiomove((caddr_t)mp->b_rptr, n,
						UIO_READ, &uio)) != 0) {
				mutex_exit(QLOCK(rdq));
				return (error);
			}
			mp = mp->b_cont;
		}
		strpeek.ctlbuf.len = strpeek.ctlbuf.maxlen - uio.uio_resid;

		/*
		 * Now process DATA blocks, if any.
		 */
		iov.iov_base = strpeek.databuf.buf;
		iov.iov_len = strpeek.databuf.maxlen;
		uio.uio_iovcnt = 1;
		uio.uio_resid = iov.iov_len;
		while (mp && uio.uio_resid) {
			if (mp->b_datap->db_type == M_DATA) {
				if ((n = MIN(uio.uio_resid,
					mp->b_wptr - mp->b_rptr)) != 0 &&
					(error = uiomove((char *)mp->b_rptr, n,
							UIO_READ, &uio)) != 0) {
					mutex_exit(QLOCK(rdq));
					return (error);
				}
			}
			mp = mp->b_cont;
		}
		mutex_exit(QLOCK(rdq));
		if (fmp == NULL) {
			infod_t infod;

			infod.d_cmd = INFOD_COPYOUT;
			infod.d_res = 0;
			infod.d_uiop = &uio;
			error = infonext(rdq, &infod);
			if (error == EINVAL || error == EBUSY)
				error = 0;
			if (error)
				return (error);
			if (! (infod.d_res & INFOD_COPYOUT)) {
				/*
				 * No data found by the infonext().
				 */
				*rvalp = 0;
				return (0);
			}
		}
		strpeek.databuf.len = strpeek.databuf.maxlen - uio.uio_resid;
		error = strcopyout((caddr_t)&strpeek, (caddr_t)arg,
		    sizeof (strpeek), STRPEEK, copyflag);
		if (error) {
			return (error);
		}
		*rvalp = 1;
		return (0);
	    }

	case I_FDINSERT:
	    {
		struct strfdinsert strfdinsert;
		struct file *resftp;
		struct stdata *resstp;
		queue_t *q;
		register long msgsize;
		long rmin, rmax;
		queue_t	*mate = NULL;

		if (stp->sd_flag & STRHUP)
			return (ENXIO);
		/* STRDERR, STWRERR and STPLEX tested above. */
		error = strcopyin((caddr_t)arg, (caddr_t)&strfdinsert,
		    sizeof (strfdinsert), STRFDINSERT, copyflag);
		if (error)
			return (error);
		if (strfdinsert.offset < 0 ||
		    (strfdinsert.offset % sizeof (queue_t *)) != 0)
			return (EINVAL);
		if ((resftp = GETF(strfdinsert.fildes)) != NULL) {
			if ((resstp = resftp->f_vnode->v_stream) == NULL) {
				RELEASEF(strfdinsert.fildes);
				return (EINVAL);
			}
		} else
			return (EINVAL);

		mutex_enter(&resstp->sd_lock);
		if (resstp->sd_flag & (STRDERR|STWRERR|STRHUP|STPLEX)) {
			error = strgeterr(resstp,
					STRDERR|STWRERR|STRHUP|STPLEX);
			mutex_exit(&resstp->sd_lock);
			RELEASEF(strfdinsert.fildes);
			return (error);
		}
		mutex_exit(&resstp->sd_lock);

		/* get read queue of stream terminus */
		claimstr(resstp->sd_wrq);
		for (q = resstp->sd_wrq->q_next; q->q_next; q = q->q_next)
			if (!STRMATED(resstp) && STREAM(q) != resstp &&
			    mate == NULL) {
				ASSERT(q->q_qinfo->qi_srvp);
				ASSERT(OTHERQ(q)->q_qinfo->qi_srvp);
				claimstr(q);
				mate = q;
			}
		q = RD(q);
		if (mate)
			releasestr(mate);
		releasestr(resstp->sd_wrq);

		if (strfdinsert.ctlbuf.len <
		    strfdinsert.offset + sizeof (queue_t *)) {
			RELEASEF(strfdinsert.fildes);
			return (EINVAL);
		}

		/*
		 * Check for legal flag value.
		 */
		if (strfdinsert.flags & ~RS_HIPRI) {
			RELEASEF(strfdinsert.fildes);
			return (EINVAL);
		}

		/* get these values from those cached in the stream head */
		mutex_enter(QLOCK(stp->sd_wrq));
		rmin = stp->sd_qn_minpsz;
		rmax = stp->sd_qn_maxpsz;
		mutex_exit(QLOCK(stp->sd_wrq));

		/*
		 * Make sure ctl and data sizes together fall within
		 * the limits of the max and min receive packet sizes
		 * and do not exceed system limit.  A negative data
		 * length means that no data part is to be sent.
		 */
		ASSERT((rmax >= 0) || (rmax == INFPSZ));
		if (rmax == 0) {
			RELEASEF(strfdinsert.fildes);
			return (ERANGE);
		}
		if ((msgsize = strfdinsert.databuf.len) < 0)
			msgsize = 0;
		if ((msgsize < rmin) ||
		    ((msgsize > rmax) && (rmax != INFPSZ)) ||
		    (strfdinsert.ctlbuf.len > strctlsz)) {
			RELEASEF(strfdinsert.fildes);
			return (ERANGE);
		}

		mutex_enter(&stp->sd_lock);
		while (!(strfdinsert.flags & RS_HIPRI) &&
		    !canputnext(stp->sd_wrq)) {
			if ((error = strwaitq(stp, WRITEWAIT, (off_t)0,
			    flag, &done)) != 0 || done) {
				mutex_exit(&stp->sd_lock);
				RELEASEF(strfdinsert.fildes);
				return (error);
			}
		}
		mutex_exit(&stp->sd_lock);

		iov.iov_base = strfdinsert.databuf.buf;
		iov.iov_len = strfdinsert.databuf.len;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_loffset = 0;
		uio.uio_segflg = (copyflag == U_TO_K) ? UIO_USERSPACE :
		    UIO_SYSSPACE;
		uio.uio_fmode = 0;
		uio.uio_resid = iov.iov_len;
		if ((error = strmakemsg(&strfdinsert.ctlbuf,
		    strfdinsert.databuf.len, &uio, stp,
		    strfdinsert.flags, &mp)) != 0 || !mp) {
			RELEASEF(strfdinsert.fildes);
			return (error);
		}

		/*
		 * Place pointer to queue 'offset' bytes from the
		 * start of the control portion of the message.
		 */

		*((queue_t **)(mp->b_rptr + strfdinsert.offset)) = q;

		/*
		 * Put message downstream.
		 */
		run_queues = 1;
		putnext(stp->sd_wrq, mp);
		queuerun();
		RELEASEF(strfdinsert.fildes);
		return (error);
	    }

	case I_SENDFD:
	    {
		struct file *fp;

		if ((fp = GETF(arg)) == NULL)
			return (EBADF);
		error = do_sendfp(stp, fp, crp);
		RELEASEF(arg);
		return (error);
	    }

	case I_RECVFD:
	case I_E_RECVFD:
	    {
		register struct strrecvfd *srf;
		union {
		    struct e_strrecvfd estrfd;	/* EFT data structure */
		    struct o_strrecvfd ostrfd;
			/* non-EFT data structure - SVR3 compatibility mode. */
		} str;

		int i, fd;

		mutex_enter(&stp->sd_lock);
		while (!(mp = getq(rdq))) {
			if (stp->sd_flag & STRHUP) {
				mutex_exit(&stp->sd_lock);
				return (ENXIO);
			}
			if ((error = strwaitq(stp, GETWAIT, (off_t)0,
			    flag, &done)) != 0 || done) {
				mutex_exit(&stp->sd_lock);
				return (error);
			}
		}
		if (mp->b_datap->db_type != M_PASSFP) {
			putback(stp, rdq, mp, mp->b_band);
			mutex_exit(&stp->sd_lock);
			return (EBADMSG);
		}
		mutex_exit(&stp->sd_lock);
		srf = (struct strrecvfd *)mp->b_rptr;
		if (error = ufalloc(0, &fd)) {
			mutex_enter(&stp->sd_lock);
			putback(stp, rdq, mp, mp->b_band);
			mutex_exit(&stp->sd_lock);
			return (error);
		}
		if (cmd == I_RECVFD) {

			/* check to see if uid/gid values are too large. */

			if ((u_long)srf->uid > (u_long)USHRT_MAX ||
			    (u_long)srf->gid > (u_long)USHRT_MAX) {
				mutex_enter(&stp->sd_lock);
				putback(stp, rdq, mp, mp->b_band);
				mutex_exit(&stp->sd_lock);
				setf(fd, NULLFP);	/* release fd entry */
				return (EOVERFLOW);
			}
			str.ostrfd.f.fd = fd;
			str.ostrfd.uid = (o_uid_t)srf->uid;
			str.ostrfd.gid = (o_gid_t)srf->gid;
			for (i = 0; i < 8; i++)
				str.ostrfd.fill[i] = 0;		/* zero pad */

			if (error = strcopyout((caddr_t)&str.ostrfd,
			    (caddr_t)arg, sizeof (struct o_strrecvfd),
			    O_STRRECVFD, copyflag)) {

				setf(fd, NULLFP);	/* release fd entry */

				mutex_enter(&stp->sd_lock);
				putback(stp, rdq, mp, mp->b_band);
				mutex_exit(&stp->sd_lock);
				return (error);
			}
		} else {		/* I_E_RECVFD */

			str.estrfd.f.fd = fd;
			str.estrfd.uid = (uid_t)srf->uid;
			str.estrfd.gid = (gid_t)srf->gid;
			for (i = 0; i < 8; i++)
				str.estrfd.fill[i] = 0;		/* zero pad */

			if (error = strcopyout((caddr_t)&str.estrfd,
			    (caddr_t)arg, sizeof (struct e_strrecvfd),
			    STRRECVFD, copyflag)) {

				setf(fd, NULLFP);	/* release fd entry */
				mutex_enter(&stp->sd_lock);
				putback(stp, rdq, mp, mp->b_band);
				mutex_exit(&stp->sd_lock);
				return (error);
			}
		}

		setf(fd, srf->f.fp);
		freemsg(mp);
		return (0);
	    }

	case I_SWROPT:
		/*
		 * Set/clear the write options. arg is a bit
		 * mask with any of the following bits set...
		 * 	SNDZERO - send zero length message
		 *	SNDPIPE - send sigpipe to process if
		 *		sd_werror is set and process is
		 *		doing a write or putmsg.
		 * The new stream head write options should reflect
		 * what is in arg.
		 */
		if (arg & ~(SNDZERO|SNDPIPE))
			return (EINVAL);

		mutex_enter(&stp->sd_lock);
		stp->sd_flag &= ~(STRSNDZERO|STRSIGPIPE);
		if (arg & SNDZERO)
			stp->sd_flag |= STRSNDZERO;
		if (arg & SNDPIPE)
			stp->sd_flag |= STRSIGPIPE;
		mutex_exit(&stp->sd_lock);
		return (0);

	case I_GWROPT:
	    {
		int wropt = 0;

		if (stp->sd_flag & STRSNDZERO)
			wropt |= SNDZERO;
		if (stp->sd_flag & STRSIGPIPE)
			wropt |= SNDPIPE;
		error = strcopyout((caddr_t)&wropt, (caddr_t)arg,
		    sizeof (wropt), STRINT, copyflag);
		return (error);
	    }

	case I_LIST:
		/*
		 * Returns all the modules found on this stream,
		 * upto the driver. If argument is NULL, return the
		 * number of modules (including driver). If argument
		 * is not NULL, copy the names into the structure
		 * provided.
		 */

	    {
		queue_t *q;
		int num_modules, space_allocated;
		struct str_list strlist;

		if (arg == NULL) { /* Return number of modules plus driver */
			q = stp->sd_wrq;
			*rvalp = stp->sd_pushcnt + 1;
		} else {
			error = strcopyin((caddr_t)arg, (caddr_t)&strlist,
			    sizeof (struct str_list), STRLIST, copyflag);
			if (error)
				return (error);
			if ((space_allocated = strlist.sl_nmods) <= 0)
				return (EINVAL);
			claimstr(stp->sd_wrq);
			q = stp->sd_wrq;
			num_modules = 0;
			while (SAMESTR(q) && (space_allocated != 0)) {
				error = strcopyout(
				    q->q_next->q_qinfo->qi_minfo->mi_idname,
				    (caddr_t)(struct str_list *)
				    strlist.sl_modlist,
				    (FMNAMESZ + 1), STRNAME, copyflag);
				if (error) {
					releasestr(stp->sd_wrq);
					return (error);
				}
				q = q->q_next;
				space_allocated--;
				num_modules++;
				strlist.sl_modlist++;
			}
			releasestr(stp->sd_wrq);
			if (SAMESTR(q))  /* Space ran out */
				return (ENOSPC);
			error = strcopyout((caddr_t)&num_modules, (caddr_t)arg,
			    sizeof (int), STRINT, copyflag);
		}
		return (error);
	    }

	case I_CKBAND:
	    {
		queue_t *q;
		qband_t *qbp;

		/*
		 * Ignores MSGNOGET.
		 */
		if ((arg < 0) || (arg >= NBAND))
			return (EINVAL);
		q = RD(stp->sd_wrq);
		mutex_enter(QLOCK(q));
		if (arg > (int)q->q_nband) {
			*rvalp = 0;
		} else {
			if (arg == 0) {
				if (q->q_first)
					*rvalp = 1;
				else
					*rvalp = 0;
			} else {
				qbp = q->q_bandp;
				while (--arg > 0)
					qbp = qbp->qb_next;
				if (qbp->qb_first)
					*rvalp = 1;
				else
					*rvalp = 0;
			}
		}
		mutex_exit(QLOCK(q));
		return (0);
	    }

	case I_GETBAND:
	    {
		int intpri;
		queue_t *q;

		/*
		 * Ignores MSGNOGET.
		 */
		q = RD(stp->sd_wrq);
		mutex_enter(QLOCK(q));
		mp = q->q_first;
		if (!mp) {
			mutex_exit(QLOCK(q));
			return (ENODATA);
		}
		intpri = (int)mp->b_band;
		error = strcopyout((caddr_t)&intpri, (caddr_t)arg, sizeof (int),
		    STRINT, copyflag);
		mutex_exit(QLOCK(q));
		return (error);
	    }

	case I_ATMARK:
	    {
		queue_t *q;

		if (arg & ~(ANYMARK|LASTMARK))
			return (EINVAL);
		q = RD(stp->sd_wrq);
		mutex_enter(&stp->sd_lock);
		mutex_enter(QLOCK(q));
		mp = q->q_first;

		/*
		 * Hack for sockets compatibility.  We need to
		 * ignore any messages at the stream head that
		 * are marked MSGNOGET and are not marked MSGMARK.
		 */
		while (mp && ((mp->b_flag & (MSGNOGET|MSGMARK)) == MSGNOGET))
			mp = mp->b_next;

		if (!mp)
			*rvalp = 0;
		else if ((arg == ANYMARK) && (mp->b_flag & MSGMARK))
			*rvalp = 1;
		else if ((arg == LASTMARK) && (mp == stp->sd_mark))
			*rvalp = 1;
		else
			*rvalp = 0;
		mutex_exit(QLOCK(q));
		mutex_exit(&stp->sd_lock);
		return (0);
	    }

	case I_CANPUT:
	    {
		char band;

		if ((arg < 0) || (arg >= NBAND))
			return (EINVAL);
		band = (char)arg;
		*rvalp = bcanputnext(stp->sd_wrq, band);
		return (0);
	    }

	case I_SETCLTIME:
	    {
		long closetime;

		error = strcopyin((caddr_t)arg, (caddr_t)&closetime,
		    sizeof (long), STRLONG, copyflag);
		if (error)
			return (error);
		if (closetime < 0)
			return (EINVAL);

		stp->sd_closetime = closetime;
		return (0);
	    }

	case I_GETCLTIME:
	    {
		long closetime;

		closetime = stp->sd_closetime;
		error = strcopyout((caddr_t)&closetime, (caddr_t)arg,
		    sizeof (long), STRLONG, copyflag);
		return (error);
	    }

	case _I_BIND_RSVD:
		/*
		 * make a held fifo node
		 */
	    {
		vnode_t *vp;
		struct vattr vattr;

		vattr.va_type = IFTOVT(S_IFIFO);
		vattr.va_mode = (S_IFIFO & MODEMASK) & ~u.u_cmask;

		vattr.va_mask = AT_TYPE | AT_MODE;

		error = vn_create((char *)arg, UIO_USERSPACE, &vattr,
				EXCL, 0, &vp, CRMKNOD);
		if (error)
			return (error);

		mutex_enter(&stp->sd_lock);
		if (stp->sd_vnfifo) {
			/*
			 * some fifo node already held by
			 * this stream.
			 */
			mutex_exit(&stp->sd_lock);
			(void) vn_remove((char *)arg, UIO_USERSPACE,
					RMFILE);
			VN_RELE(vp);
			return (EINVAL);
		}
		stp->sd_vnfifo = vp;
		mutex_exit(&stp->sd_lock);

		return (0);
	    }

	case _I_RELE_RSVD:
	{
		mutex_enter(&stp->sd_lock);
		if (stp->sd_vnfifo) {
			struct vnode *vnfifo;

			vnfifo = stp->sd_vnfifo;
			stp->sd_vnfifo = NULL;
			mutex_exit(&stp->sd_lock);
			/*
			 * Note: VN_RELE acquires v_lock
			 */
			VN_RELE(vnfifo);
			return (0);
		} else {
			mutex_exit(&stp->sd_lock);
			return (EINVAL);
		}
	}

	case TIOCSSID:
	{
		pid_t sid;
		register error;

		if (error = strcopyin((caddr_t)arg, (caddr_t)&sid,
		    sizeof (pid_t), STRPIDT, copyflag))
			return (error);
		mutex_enter(&pidlock);
		if (stp->sd_sidp != ttoproc(curthread)->p_sessp->s_sidp) {
			mutex_exit(&pidlock);
			return (ENOTTY);
		}
		mutex_exit(&pidlock);
		return (realloctty(ttoproc(curthread), sid));
	}

	case TIOCGSID:
	{
		pid_t sid;

		mutex_enter(&pidlock);
		if (stp->sd_sidp == NULL) {
			mutex_exit(&pidlock);
			return (ENOTTY);
		}
		sid = stp->sd_sidp->pid_id;
		mutex_exit(&pidlock);
		return strcopyout((caddr_t)&sid,
		    (caddr_t)arg, sizeof (pid_t), STRPIDT, copyflag);
	}

	case TIOCSPGRP:
	{
		pid_t pgrp;
		proc_t *q;
		pid_t	sid, fg_pgid, bg_pgid;

		if (error = strcopyin((caddr_t)arg, (caddr_t)&pgrp,
		    sizeof (pid_t), STRPIDT, copyflag))
			return (error);
		mutex_enter(&stp->sd_lock);
		mutex_enter(&pidlock);
		if (stp->sd_sidp != ttoproc(curthread)->p_sessp->s_sidp) {
			mutex_exit(&pidlock);
			mutex_exit(&stp->sd_lock);
			return (ENOTTY);
		}
		if (pgrp == stp->sd_pgidp->pid_id) {
			mutex_exit(&pidlock);
			mutex_exit(&stp->sd_lock);
			return (0);
		}
		if (pgrp <= 0 || pgrp >= MAXPID) {
			mutex_exit(&pidlock);
			mutex_exit(&stp->sd_lock);
			return (EINVAL);
		}
		if ((q = pgfind(pgrp)) == NULL ||
		    q->p_sessp != ttoproc(curthread)->p_sessp) {
			mutex_exit(&pidlock);
			mutex_exit(&stp->sd_lock);
			return (EPERM);
		}
		sid = stp->sd_sidp->pid_id;
		fg_pgid = q->p_pgrp;
		bg_pgid = stp->sd_pgidp->pid_id;
		CL_SET_PROCESS_GROUP(curthread, sid, bg_pgid, fg_pgid);
		PID_RELE(stp->sd_pgidp);
		stp->sd_pgidp = q->p_pgidp;
		PID_HOLD(stp->sd_pgidp);
		mutex_exit(&pidlock);
		mutex_exit(&stp->sd_lock);
		return (0);
	}

	case TIOCGPGRP:
	{
		pid_t pgrp;

		mutex_enter(&pidlock);
		if (stp->sd_sidp == NULL) {
			mutex_exit(&pidlock);
			return (ENOTTY);
		}
		pgrp = stp->sd_pgidp->pid_id;
		mutex_exit(&pidlock);
		return strcopyout((caddr_t)&pgrp,
		    (caddr_t)arg, sizeof (pid_t), STRPIDT, copyflag);
	}

	case FIONBIO:
	case FIOASYNC:
		return (0);	/* handled by the upper layer */
	}
}
/* ARGSUSED */
int
do_sendfp(
	struct stdata *stp,
	struct file *fp,
	struct cred *cr
)
{
	register queue_t *qp;
	register struct strrecvfd *srf;
	mblk_t *mp;
	queue_t	*mate = NULL;

	if (stp->sd_flag & STRHUP)
		return (ENXIO);

	claimstr(stp->sd_wrq);
	for (qp = stp->sd_wrq; qp->q_next; qp = qp->q_next) {
		if (!STRMATED(stp) && STREAM(qp->q_next) != stp &&
		    mate == NULL) {
			ASSERT(qp->q_qinfo->qi_srvp);
			ASSERT(OTHERQ(qp)->q_qinfo->qi_srvp);
			ASSERT(OTHERQ(qp->q_next)->q_qinfo->qi_srvp);
			claimstr(qp->q_next);
			mate = qp->q_next;
		}
	}

	/* XXX prevents substitution of the ops vector */
	if (qp->q_qinfo != &strdata && qp->q_qinfo != &fifo_strdata) {
		ASSERT(mate == NULL);
		releasestr(stp->sd_wrq);
		return (EINVAL);
	}

	if ((qp->q_flag & QFULL) ||
	    !(mp = allocb(sizeof (struct strrecvfd), BPRI_MED))) {
		releasestr(stp->sd_wrq);
		if (mate)
			releasestr(mate);
		return (EAGAIN);
	}
	srf = (struct strrecvfd *)mp->b_rptr;
	mp->b_wptr += sizeof (struct strrecvfd);
	mp->b_datap->db_type = M_PASSFP;
	srf->f.fp = fp;
	srf->uid = curthread->t_cred->cr_uid;
	srf->gid = curthread->t_cred->cr_gid;
	mutex_enter(&fp->f_tlock);
	fp->f_count++;
	mutex_exit(&fp->f_tlock);
	/*
	 * Should the M_PASSFP message get stuck on the syncq
	 * (due to a close off the other side) then flush_syncq will take care
	 * of freeing the M_PASSFP using freemsg_flush.
	 */
	put(qp, mp);
	releasestr(stp->sd_wrq);
	if (mate)
		releasestr(mate);
	return (0);
}

/*
 * Send an ioctl message downstream and wait for acknowledgement.
 * flags may be set to either U_TO_K or K_TO_K and a combination
 * of STR_NOERROR or STR_NOSIG
 * STR_NOSIG: Signals are essentially ignored or held and have
 *	no effect for the duration of the call.
 * STR_NOERROR: Ignores stream head read, write and hup errors.
 *	Additionally, if an existing ioctl times out, it is assumed
 *	lost and and this ioctl will continue as if the previous ioctl had
 *	finished.  ETIME may be returned if this ioctl times out (i.e.
 *	ic_timout is not INFTIM).  Non-stream head errors may be returned if
 *	the ioc_error indicates that the driver/module had problems,
 *	an EFAULT was found when accessing user data, a lack of
 * 	resources, etc.
 */
int
strdoioctl(
	struct stdata *stp,
	struct strioctl *strioc,
	mblk_t *ebp,		/* extended data for ioctl */
	int flag,
	char *fmtp,
	cred_t *crp,
	int *rvalp
)
{
	mblk_t *bp;
	struct iocblk *iocbp;
	struct copyreq *reqp;
	struct copyresp *resp;
	mblk_t *fmtbp;
	int id;
	int transparent = 0;
	int error = 0;
	int len = 0;
	caddr_t taddr;
	int copyflag = (flag & (U_TO_K | K_TO_K));
	int sigflag = (flag & STR_NOSIG);
	int errs;

	ASSERT(copyflag == U_TO_K || copyflag == K_TO_K);

	TRACE_2(TR_FAC_STREAMS_FR,
		TR_STRDOIOCTL,
		"strdoioctl:stp %X cmd %X", stp, strioc->ic_cmd);
	if (strioc->ic_len == TRANSPARENT) {	/* send arg in M_DATA block */
		transparent = 1;
		strioc->ic_len = sizeof (int);
	}

	if ((strioc->ic_len < 0) ||
	    ((strmsgsz > 0) && (strioc->ic_len > strmsgsz))) {
		if (ebp)
			freeb(ebp);
		return (EINVAL);
	}

	if (!(bp = allocb_wait(max(sizeof (struct iocblk),
	    sizeof (struct copyreq)), BPRI_HI, sigflag, &error))) {
			if (ebp)
				freeb(ebp);
			return (error);
	}

	iocbp = (struct iocblk *)bp->b_wptr;
	iocbp->ioc_count = strioc->ic_len;
	iocbp->ioc_cmd = strioc->ic_cmd;
	crhold(crp);
	iocbp->ioc_cr = crp;
	iocbp->ioc_error = 0;
	iocbp->ioc_rval = 0;
	bp->b_datap->db_type = M_IOCTL;
	bp->b_wptr += sizeof (struct iocblk);

	if (flag & STR_NOERROR)
		errs = STPLEX;
	else
		errs = STRHUP|STRDERR|STWRERR|STPLEX;

	/*
	 * If there is data to copy into ioctl block, do so.
	 */
	if (iocbp->ioc_count) {
		if (transparent)
			/*
			 * Note: STR_NOERROR does not have an effect
			 * in putiocd()
			 */
			id = K_TO_K | sigflag;
		else
			id = flag;
		if (error = putiocd(bp, ebp, strioc->ic_dp, id, fmtp)) {
			freemsg(bp);
			if (ebp)
				freeb(ebp);
			crfree(crp);
			return (error);
		}

		/*
		 * We could have slept copying in user pages.
		 * Recheck the stream head state (the other end
		 * of a pipe could have gone away).
		 */
		mutex_enter(&stp->sd_lock);
		if (stp->sd_flag & errs) {
			error = strgeterr(stp, errs);
			mutex_exit(&stp->sd_lock);
			freemsg(bp);
			crfree(crp);
			return (error);
		}
	} else {
		bp->b_cont = ebp;
		mutex_enter(&stp->sd_lock);
	}
	if (transparent)
		iocbp->ioc_count = TRANSPARENT;

	/*
	 * Block for up to STRTIMOUT milliseconds if there is an outstanding
	 * ioctl for this stream already pending.  All processes
	 * sleeping here will be awakened as a result of an ACK
	 * or NAK being received for the outstanding ioctl, or
	 * as a result of the timer expiring on the outstanding
	 * ioctl (a failure), or as a result of any waiting
	 * process's timer expiring (also a failure).
	 */

	error = 0;

	while (stp->sd_flag & IOCWAIT) {
		int cv_rval;

		TRACE_0(TR_FAC_STREAMS_FR,
			TR_STRDOIOCTL_WAIT,
			"strdoioctl sleeps - IOCWAIT");
		cv_rval = str_cv_wait(&stp->sd_iocmonitor, &stp->sd_lock,
		    (u_long)STRTIMOUT, sigflag);
		if (cv_rval <= 0) {
			if (cv_rval == 0) {
				error = EINTR;
			} else {
				if (flag & STR_NOERROR) {
					/*
					 * Trashing pervious IOCWAIT
					 * We'll assume it got lost
					 */
					stp->sd_flag &= ~IOCWAIT;
				} else {
					/*
					 * pending ioctl has caused
					 * us to time out
					 */
					error =  ETIME;
				}
			}
		} else if ((stp->sd_flag & errs)) {
			error = strgeterr(stp, errs);
		}
		if (error) {
			mutex_exit(&stp->sd_lock);
			freemsg(bp);
			crfree(crp);
			return (error);
		}
	}

	/*
	 * Have control of ioctl mechanism.
	 * Send down ioctl packet and wait for response.
	 */
	if (stp->sd_iocblk) {
		freemsg(stp->sd_iocblk);
		stp->sd_iocblk = NULL;
	}

	stp->sd_flag |= IOCWAIT;

	/*
	 * Assign sequence number.
	 */
	iocbp->ioc_id = stp->sd_iocid = getiocseqno();

	mutex_exit(&stp->sd_lock);

	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STRDOIOCTL_PUT, "strdoioctl puts to:%X",
			stp->sd_wrq->q_next);
	run_queues = 1;
	putnext(stp->sd_wrq, bp);
	queuerun();

	/*
	 * Timed wait for acknowledgment.  The wait time is limited by the
	 * timeout value, which must be a positive integer (number of
	 * milliseconds to wait, or 0 (use default value of STRTIMOUT
	 * milliseconds), or -1 (wait forever).  This will be awakened
	 * either by an ACK/NAK message arriving, the timer expiring, or
	 * the timer expiring on another ioctl waiting for control of the
	 * mechanism.
	 */
waitioc:
	mutex_enter(&stp->sd_lock);


	/*
	 * If the reply has already arrived, don't sleep.  If awakened from
	 * the sleep, fail only if the reply has not arrived by then.
	 * Otherwise, process the reply.
	 */
	while (!stp->sd_iocblk) {
		int cv_rval;

		if (stp->sd_flag & errs) {
			error = strgeterr(stp, errs);
			stp->sd_flag &= ~IOCWAIT;
			cv_broadcast(&stp->sd_iocmonitor);
			mutex_exit(&stp->sd_lock);
			crfree(crp);
			return (error);
		}

		TRACE_0(TR_FAC_STREAMS_FR,
			TR_STRDOIOCTL_WAIT2,
			"strdoioctl sleeps awaiting reply");
		ASSERT(error == 0);

		cv_rval = str_cv_wait(&stp->sd_monitor, &stp->sd_lock,
		    (u_long)(strioc->ic_timout ? strioc->ic_timout: STRTIMOUT),
		    sigflag);
		/*
		 * note: STR_NOERROR does not protect
		 * us here.. use ic_timout < 0
		 */
		if (cv_rval <= 0) {
			if (cv_rval == 0) {
				error = EINTR;
			} else {
				error =  ETIME;
			}
			bp = NULL;
			/*
			 * A message could have come in after we were scheduled
			 * but before we were actually run.
			 */
			if (stp->sd_iocblk) {
				bp = stp->sd_iocblk;
				stp->sd_iocblk = NULL;
			}
			stp->sd_flag &= ~IOCWAIT;
			cv_broadcast(&stp->sd_iocmonitor);
			mutex_exit(&stp->sd_lock);
			if (bp) {
				if ((bp->b_datap->db_type == M_COPYIN) ||
				    (bp->b_datap->db_type == M_COPYOUT)) {
					if (bp->b_cont) {
						freemsg(bp->b_cont);
						bp->b_cont = NULL;
					}
					bp->b_datap->db_type = M_IOCDATA;
					resp = (struct copyresp *)bp->b_rptr;
					resp->cp_rval =
					    (caddr_t)1; /* failure */
					run_queues = 1;
					putnext(stp->sd_wrq, bp);
					queuerun();
				} else
					freemsg(bp);
			}
			crfree(crp);
			return (error);
		}
	}
	ASSERT(stp->sd_iocblk);
	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STRDOIOCTL_ACK, "strdoioctl got reply:%X",
			bp->b_datap->db_type);
	bp = stp->sd_iocblk;
	stp->sd_iocblk = NULL;
	if ((bp->b_datap->db_type == M_IOCACK) ||
	    (bp->b_datap->db_type == M_IOCNAK)) {
		stp->sd_flag &= ~IOCWAIT;
		cv_broadcast(&stp->sd_iocmonitor);
		mutex_exit(&stp->sd_lock);
	}
	else
		mutex_exit(&stp->sd_lock);


	/*
	 * Have received acknowlegment.
	 */

	switch (bp->b_datap->db_type) {
	case M_IOCACK:
		/*
		 * Positive ack.
		 */
		iocbp = (struct iocblk *)bp->b_rptr;

		/*
		 * Set error if indicated.
		 */
		if (iocbp->ioc_error) {
			error = iocbp->ioc_error;
			break;
		}

		/*
		 * Set return value.
		 */
		*rvalp = iocbp->ioc_rval;

		/*
		 * Data may have been returned in ACK message (ioc_count > 0).
		 * If so, copy it out to the user's buffer.
		 */
		if (iocbp->ioc_count && !transparent) {
			if (strioc->ic_cmd == TCGETA ||
			    strioc->ic_cmd == TCGETS ||
			    strioc->ic_cmd == TIOCGETP ||
			    strioc->ic_cmd == LDGETT) {
				if (error = getiocd(bp, strioc->ic_dp,
				    copyflag, fmtp))
					break;
			} else if (error = getiocd(bp, strioc->ic_dp,
			    copyflag, (char *)NULL)) {
				break;
			}
		}
		if (!transparent) {
			if (len)	/* an M_COPYOUT was used with I_STR */
				strioc->ic_len = len;
			else
				strioc->ic_len = iocbp->ioc_count;
		}
		break;

	case M_IOCNAK:
		/*
		 * Negative ack.
		 *
		 * The only thing to do is set error as specified
		 * in neg ack packet.
		 */
		iocbp = (struct iocblk *)bp->b_rptr;

		error = (iocbp->ioc_error ? iocbp->ioc_error : EINVAL);
		break;

	case M_COPYIN:
		/*
		 * Driver or module has requested user ioctl data.
		 */
		reqp = (struct copyreq *)bp->b_rptr;
		fmtbp = bp->b_cont;
		bp->b_cont = NULL;
		if (reqp->cq_flag & RECOPY) {
			/* redo I_STR copyin with canonical processing */
			ASSERT(fmtbp);
			reqp->cq_size = strioc->ic_len;
			error = putiocd(bp, NULL, strioc->ic_dp, flag,
			    (fmtbp ? (char *)fmtbp->b_rptr : (char *)NULL));
			if (fmtbp)
				freeb(fmtbp);
		} else if (reqp->cq_flag & STRCANON) {
			/* copyin with canonical processing */
			ASSERT(fmtbp);
			error = putiocd(bp, NULL, reqp->cq_addr, flag,
			    (fmtbp ? (char *)fmtbp->b_rptr : (char *)NULL));
			if (fmtbp)
				freeb(fmtbp);
		} else {
			ASSERT(fmtbp == NULL);
			freemsg(fmtbp);
			/* copyin raw data (i.e. no canonical processing) */
			error = putiocd(bp, NULL, reqp->cq_addr, flag,
			    (char *)NULL);
		}
		if (error && bp->b_cont) {
			freemsg(bp->b_cont);
			bp->b_cont = NULL;
		}

		bp->b_wptr = bp->b_rptr + sizeof (struct copyresp);
		bp->b_datap->db_type = M_IOCDATA;
		resp = (struct copyresp *)bp->b_rptr;
		resp->cp_rval = (caddr_t)error;

		run_queues = 1;
		putnext(stp->sd_wrq, bp);
		queuerun();

		if (error) {
			mutex_enter(&stp->sd_lock);
			stp->sd_flag &= ~IOCWAIT;
			cv_broadcast(&stp->sd_iocmonitor);
			mutex_exit(&stp->sd_lock);
			crfree(crp);
			return (error);
		}

		goto waitioc;

	case M_COPYOUT:
		/*
		 * Driver or module has ioctl data for a user.
		 */
		reqp = (struct copyreq *)bp->b_rptr;
		ASSERT(bp->b_cont);
		if (transparent) {
			taddr = reqp->cq_addr;
		} else {
			taddr = strioc->ic_dp;
			len = reqp->cq_size;
		}
		if (reqp->cq_flag & STRCANON) {
			/* copyout with canonical processing */
			if ((fmtbp = bp->b_cont) != NULL) {
				bp->b_cont = fmtbp->b_cont;
				fmtbp->b_cont = NULL;
			}
			error = getiocd(bp, taddr, copyflag,
			    (fmtbp ? (char *)fmtbp->b_rptr : (char *)NULL));
			if (fmtbp)
				freeb(fmtbp);
		} else {
			/* copyout raw data (i.e. no canonical processing) */
			error = getiocd(bp, taddr, copyflag, (char *)NULL);
		}
		freemsg(bp->b_cont);
		bp->b_cont = NULL;

		bp->b_wptr = bp->b_rptr + sizeof (struct copyresp);
		bp->b_datap->db_type = M_IOCDATA;
		resp = (struct copyresp *)bp->b_rptr;
		resp->cp_rval = (caddr_t)error;

		run_queues = 1;
		putnext(stp->sd_wrq, bp);
		queuerun();

		if (error) {
			mutex_enter(&stp->sd_lock);
			stp->sd_flag &= ~IOCWAIT;
			cv_broadcast(&stp->sd_iocmonitor);
			mutex_exit(&stp->sd_lock);
			crfree(crp);
			return (error);
		}
		goto waitioc;

	default:
		ASSERT(0);
		break;
	}

	freemsg(bp);
	crfree(crp);
	return (error);
}

/*
 * For the SunOS keyboard driver.
 * Return the next available "ioctl" sequence number.
 * Exported, so that streams modules can send "ioctl" messages
 * downstream from their open routine.
 */
int
getiocseqno(void)
{
	int	i;

	mutex_enter(&strresources);
	i = ++ioc_id;
	mutex_exit(&strresources);
	return (i);
}

/*
 * Get the next message from the read queue.  If the message is
 * priority, STRPRI will have been set by strrput().  This flag
 * should be reset only when the entire message at the front of the
 * queue as been consumed.
 */
int
strgetmsg(
	register struct vnode *vp,
	register struct strbuf *mctl,
	register struct strbuf *mdata,
	unsigned char *prip,
	int *flagsp,
	int fmode,
	rval_t *rvp
)
{
	register struct stdata *stp;
	register mblk_t *bp, *nbp;
	mblk_t *savemp = NULL;
	mblk_t *savemptail = NULL;
	int n, bcnt;
	int done = 0;
	int flg = 0;
	int more = 0;
	int error = 0;
	char rwflg = 0;
	void *canrw;
	char *ubuf;
	int mark;
	unsigned char pri;
	queue_t *q;
	int	pr = 0;			/* Partial read successful */

	ASSERT(vp->v_stream);
	stp = vp->v_stream;
	canrw = stp->sd_struiordq;
	rvp->r_val1 = 0;

	if (stp->sd_sidp != NULL && stp->sd_vnode->v_type != VFIFO)
		if (error = straccess(stp, JCREAD))
			return (error);

	/* Fast check of flags before acquiring the lock */
	if (stp->sd_flag & (STRDERR|STPLEX)) {
		mutex_enter(&stp->sd_lock);
		error = strgeterr(stp, STRDERR|STPLEX);
		mutex_exit(&stp->sd_lock);
		if (error != 0)
			return (error);
	}

	switch (*flagsp) {
	case MSG_HIPRI:
		if (*prip != 0)
			return (EINVAL);
		break;

	case MSG_ANY:
	case MSG_BAND:
		break;

	default:
		return (EINVAL);
	}

	q = RD(stp->sd_wrq);
	mutex_enter(&stp->sd_lock);
	mark = 0;
	while (((*flagsp & MSG_HIPRI) && !(stp->sd_flag & STRPRI)) ||
	    ((*flagsp & MSG_BAND) && (!q->q_first ||
	    ((q->q_first->b_band < *prip) && !(stp->sd_flag & STRPRI)))) ||
	    !(bp = getq(q))) {
		if (canrw && rwflg && mdata->maxlen >= 0) {
			/*
			 * Stream supports rwnext() for the read side.
			 */
			struiod_t uiod;

			uiod.d_iov[0].iov_base = mdata->buf;
			uiod.d_iov[0].iov_len = mdata->maxlen;
			uiod.d_uio.uio_iov = uiod.d_iov;
			uiod.d_uio.uio_iovcnt = 1;
			uiod.d_uio.uio_loffset = 0;
			uiod.d_uio.uio_segflg = UIO_USERSPACE;
			uiod.d_uio.uio_fmode = 0;
			uiod.d_uio.uio_resid = mdata->maxlen;
			uiod.d_uio.uio_offset = 0;
			uiod.d_mp = 0;
			stp->sd_struiodnak++;
			error = rwnext(q, &uiod);
			mutex_enter(&stp->sd_lock);
			stp->sd_struiodnak--;
			while (!stp->sd_struiodnak &&
			    (bp = stp->sd_struionak)) {
				stp->sd_struionak = bp->b_next;
				bp->b_next = NULL;
				bp->b_datap->db_type = M_IOCNAK;
				if (OTHERQ(q)->q_next == NULL)
					freemsg(bp);
				else {
					mutex_exit(&stp->sd_lock);
					qreply(q, bp);
					mutex_enter(&stp->sd_lock);
				}
			}
			if (!error) {
				if ((bp = uiod.d_mp) != NULL) {
					rwflg = 0;
					pri = bp->b_band;
					goto ismdata;
				}
				if (bp = getq(q))
					/*
					 * A rwnext() generated mblk
					 * has bubbled up via strrput().
					 */
					break;
			} else if (error == EINVAL) {
				/*
				 * The stream plumbing must have
				 * changed while we where away, so
				 * just turn off rwnext()s.
				 */
				error = 0;
				canrw = 0;
				if (bp = getq(q))
					/*
					 * A rwnext() generated mblk
					 * has bubbled up via strrput().
					 */
					break;
			} else if (error == EBUSY) {
				error = 0;
				if (bp = getq(q))
					/*
					 * A rwnext() generated mblk
					 * has bubbled up via strrput().
					 */
					break;
			} else {
				mutex_exit(&stp->sd_lock);
				return (error);
			}
		}
		/*
		 * If STRHUP, return 0 length control and data.
		 */
		if (stp->sd_flag & STRHUP) {
			mctl->len = mdata->len = 0;
			*flagsp = flg;
			mutex_exit(&stp->sd_lock);
			return (error);
		}
		if (((error = strwaitq(stp, GETWAIT, (off_t)0, fmode, &done))
			!= 0) || done) {
			mutex_exit(&stp->sd_lock);
			return (error);
		}
		/*
		 * Turn back on rwnext().
		 */
		rwflg = 1;
	}
	if (bp == stp->sd_mark) {
		mark = 1;
		stp->sd_mark = NULL;
	}

	pri = bp->b_band;
	if (bp->b_datap->db_type == M_PASSFP) {
		if (mark && !stp->sd_mark)
			stp->sd_mark = bp;
		putback(stp, q, bp, pri);
		mutex_exit(&stp->sd_lock);
		return (EBADMSG);
	}
ismdata:
	mutex_exit(&stp->sd_lock);

	if (qready())
		queuerun();

	/*
	 * Set HIPRI flag if message is priority.
	 */
	if (stp->sd_flag & STRPRI)
		flg = MSG_HIPRI;
	else
		flg = MSG_BAND;

	/*
	 * First process PROTO or PCPROTO blocks, if any.
	 */
	if (mctl->maxlen >= 0 && bp && bp->b_datap->db_type != M_DATA) {
		bcnt = mctl->maxlen;
		ubuf = mctl->buf;
		while (bp && bp->b_datap->db_type != M_DATA && bcnt >= 0) {
			if ((n = MIN(bcnt, bp->b_wptr - bp->b_rptr)) != 0 &&
			    copyout((caddr_t)bp->b_rptr, ubuf, n)) {
				error = EFAULT;
				mutex_enter(&stp->sd_lock);
				stp->sd_flag &= ~STRPRI;
				more = 0;
				freemsg(bp);
				goto getmout;
			}
			ubuf += n;
			bp->b_rptr += n;
			if (bp->b_rptr >= bp->b_wptr) {
				nbp = bp;
				bp = bp->b_cont;
				freeb(nbp);
			}
			if ((bcnt -= n) <= 0)
				break;
		}
		mctl->len = mctl->maxlen - bcnt;
	} else
		mctl->len = -1;


	if (bp && bp->b_datap->db_type != M_DATA) {
		/*
		 * More PROTO blocks in msg.
		 */
		more |= MORECTL;
		savemp = bp;
		while (bp && bp->b_datap->db_type != M_DATA) {
			savemptail = bp;
			bp = bp->b_cont;
		}
		savemptail->b_cont = NULL;
	}

	/*
	 * Now process DATA blocks, if any.
	 */
	if (mdata->maxlen >= 0 && bp) {
		bcnt = mdata->maxlen;
		ubuf = mdata->buf;
		while (bp && bcnt >= 0) {
			dblk_t *dp = bp->b_datap;

			if (canrw &&
			    (dp->db_struioflag & STRUIO_SPEC) &&
			    (bp->b_rptr <= dp->db_struiobase) &&
			    (bp->b_wptr >= dp->db_struiolim)) {
				/*
				 * This is an mblk that may have had
				 * part of it uiomove()ed already, so
				 * we have to handle up to three cases:
				 *
				 *    1) data prefixed to the uio data
				 *	     rptr thru (uiobase - 1)
				 *    2) uio data already uiomove()ed
				 *	     uiobase thru (uioptr - 1)
				 *    3) uio data not uiomove()ed and
				 *	 data suffixed to the uio data
				 *	     uioptr thru (wptr - 1)
				 *
				 * That is, this mblk may be proccesed
				 * up to three times, one for each case.
				 */
				/* LINTED - statement has no conseq */
				if ((n = dp->db_struiobase - bp->b_rptr) > 0) {
					/*
					 * Prefixed data.
					 */
					;
				} else if ((n = dp->db_struioptr -
						dp->db_struiobase) > 0) {
					/*
					 * Uio data already uiomove()ed.
					 */
					ASSERT(n <= bcnt);
					ASSERT(dp->db_struiobase == bp->b_rptr);
					dp->db_struiobase += n;
					goto skip;
				} else if ((n = bp->b_wptr -
						dp->db_struioptr) > 0) {
					/*
					 * Uio data not uiomove()ed
					 * and/or suffixed data.
					 */
					ASSERT(dp->db_struioptr == bp->b_rptr);
					dp->db_struiobase += n;
					dp->db_struioptr += n;
				} else if ((n = bp->b_wptr - bp->b_rptr) > 0) {
					/*
					 * If n is +, one of the above
					 * branches should have been
					 * taken.  If it is -, this
					 * is a bad thing!
					 */
					cmn_err(CE_PANIC,
					    "strgetmsg: STRUIO %x %x",
					    (int)bp, (int)bcnt);
				} else {
					/*
					 * Zero length mblk, skip it.
					 */
					goto skip;
				}
			} else {
				n = bp->b_wptr - bp->b_rptr;
			}
			if ((n = MIN(bcnt, n)) != 0 &&
			    copyout((caddr_t)bp->b_rptr, ubuf, n)) {
				error = EFAULT;
				mutex_enter(&stp->sd_lock);
				stp->sd_flag &= ~STRPRI;
				more = 0;
				freemsg(bp);
				goto getmout;
			}
		skip:;
			/*
			 * (pr == 1) indicates a partial read.
			 */
			if (n > 0)
				pr = 1;
			ubuf += n;
			bp->b_rptr += n;
			if (bp->b_rptr >= bp->b_wptr) {
				nbp = bp;
				bp = bp->b_cont;
				freeb(nbp);
			}
			if ((bcnt -= n) <= 0)
				break;
		}
		mdata->len = mdata->maxlen - bcnt;
	} else
		mdata->len = -1;

	if (bp) {			/* more data blocks in msg */
		more |= MOREDATA;
		nbp = bp;
		do {
			nbp->b_datap->db_struioflag = 0;
		} while ((nbp = nbp->b_cont) != NULL);
		if (savemp)
			savemptail->b_cont = bp;
		else
			savemp = bp;
	}

	mutex_enter(&stp->sd_lock);
	if (savemp) {
		savemp->b_band = pri;
		if (mark && !stp->sd_mark) {
			savemp->b_flag |= MSGMARK;
			stp->sd_mark = savemp;
		}
		if (pr && (savemp->b_datap->db_type == M_DATA) &&
			(msgdsize(savemp) == 0)) {
			freemsg(savemp);
		} else
			putback(stp, q, savemp, pri);
	} else {
		stp->sd_flag &= ~STRPRI;
	}

	*flagsp = flg;
	*prip = pri;

	/*
	 * Getmsg cleanup processing - if the state of the queue has changed
	 * some signals may need to be sent and/or poll awakened.
	 */
getmout:
	ASSERT(MUTEX_HELD(&stp->sd_lock));
	while ((bp = q->q_first) != NULL &&
	    (bp->b_datap->db_type == M_SIG) &&
	    (bp = getq(q)) != NULL) {
		if (bp->b_datap->db_type == M_SIG) {
			mutex_exit(&stp->sd_lock);
			strsignal(stp, *bp->b_rptr, (long)bp->b_band);
			freemsg(bp);
			if (qready())
				queuerun();
			mutex_enter(&stp->sd_lock);
		}
	}

	/*
	 * If we have just received a high priority message and a
	 * regular message is now at the front of the queue, send
	 * signals in S_INPUT processes and wake up processes polling
	 * on POLLIN.
	 */
	if ((bp = q->q_first) != NULL && !(bp->b_flag & MSGNOGET) &&
	    !(stp->sd_flag & STRPRI)) {
	    if (flg & MSG_HIPRI) {
		if (stp->sd_sigflags & S_INPUT)
			strsendsig(stp->sd_siglist, S_INPUT, (long)bp->b_band);
		if (bp->b_band == 0) {
		    if (stp->sd_sigflags & S_RDNORM)
			    strsendsig(stp->sd_siglist, S_RDNORM, 0L);
		} else {
		    if (stp->sd_sigflags & S_RDBAND)
			    strsendsig(stp->sd_siglist, S_RDBAND,
				(long)bp->b_band);
		}
		if (bp->b_band == 0) {
			if (stp->sd_flag & STRPOLL) {
				stp->sd_flag &= ~STRPOLL;
				mutex_exit(&stp->sd_lock);
				pollwakeup_safe(&stp->sd_pollist,
					POLLIN | POLLRDNORM);
			} else
				mutex_exit(&stp->sd_lock);
		} else {
			mutex_exit(&stp->sd_lock);
			pollwakeup_safe(&stp->sd_pollist, POLLIN | POLLRDBAND);
		}
	    } else {
		if (pri != bp->b_band) {
		    if (bp->b_band == 0) {
			if (stp->sd_sigflags & S_RDNORM)
				strsendsig(stp->sd_siglist, S_RDNORM, 0L);
		    } else {
			if (stp->sd_sigflags & S_RDBAND)
				strsendsig(stp->sd_siglist, S_RDBAND,
				    (long)bp->b_band);
		    }
		    if (bp->b_band == 0) {
			if (stp->sd_flag & STRPOLL) {
				stp->sd_flag &= ~STRPOLL;
				mutex_exit(&stp->sd_lock);
				pollwakeup_safe(&stp->sd_pollist,
					POLLIN | POLLRDNORM);
			} else
				mutex_exit(&stp->sd_lock);
		    } else {
			mutex_exit(&stp->sd_lock);
			pollwakeup_safe(&stp->sd_pollist, POLLIN | POLLRDBAND);
		    }
		} else {
			mutex_exit(&stp->sd_lock);
		}
	    }
	} else {
		mutex_exit(&stp->sd_lock);
	}

	rvp->r_val1 = more;
	return (error);
}

/*
 * Put a message downstream.
 */
int
strputmsg(
	register struct vnode *vp,
	register struct strbuf *mctl,
	register struct strbuf *mdata,
	unsigned char pri,
	register flag,
	int fmode
)
{
	register struct stdata *stp;
	mblk_t *mp;
	register long msgsize;
	long rmin, rmax;
	int error, done;
	void *canrw;
	struiod_t uiod;

	ASSERT(vp->v_stream);
	stp = vp->v_stream;
	canrw = stp->sd_struiowrq;
#ifdef C2_AUDIT
	if (audit_active)
		audit_strputmsg(vp, mctl, mdata, pri, flag, fmode);
#endif

	if (stp->sd_sidp != NULL && stp->sd_vnode->v_type != VFIFO)
		if (error = straccess(stp, JCWRITE))
			return (error);

	/* Fast check of flags before acquiring the lock */
	if (stp->sd_flag & (STWRERR|STRHUP|STPLEX)) {
		mutex_enter(&stp->sd_lock);
		error = strgeterr(stp, STWRERR|STRHUP|STPLEX);
		mutex_exit(&stp->sd_lock);
		if (error != 0) {
			if (!(stp->sd_flag & STPLEX) &&
			    (stp->sd_flag & STRSIGPIPE))
				psignal(ttoproc(curthread), SIGPIPE);
			return (error);
		}
	}

	/*
	 * Check for legal flag value.
	 */
	switch (flag) {
	case MSG_HIPRI:
		if ((mctl->len < 0) || (pri != 0))
			return (EINVAL);
		break;
	case MSG_BAND:
		break;

	default:
		return (EINVAL);
	}

	TRACE_1(TR_FAC_STREAMS_FR, TR_STRPUTMSG_IN,
		"strputmsg in:q %X", stp->sd_wrq);

	/* get these values from those cached in the stream head */
	rmin = stp->sd_qn_minpsz;
	rmax = stp->sd_qn_maxpsz;

	/*
	 * Make sure ctl and data sizes together fall within the
	 * limits of the max and min receive packet sizes and do
	 * not exceed system limit.
	 */
	ASSERT((rmax >= 0) || (rmax == INFPSZ));
	if (rmax == 0) {
		return (ERANGE);
	}
	if ((msgsize = mdata->len) < 0) {
		msgsize = 0;
		rmin = 0;	/* no range check for NULL data part */
	}
	if ((msgsize < rmin) ||
	    ((msgsize > rmax) && (rmax != INFPSZ)) ||
	    (mctl->len > strctlsz)) {
		return (ERANGE);
	}

	/*
	 *  if a message is being "held" awaiting possible consolidation,
	 *  send it downstream before proceeding. Don't need to do canput
	 *  as must have been OK to be put there in the first place
	 */
	mutex_enter(&stp->sd_lock);
	while (!canrw && !(flag&MSG_HIPRI) && !bcanputnext(stp->sd_wrq, pri)) {
	    TRACE_2(TR_FAC_STREAMS_FR, TR_STRPUTMSG_WAIT,
		"strputmsg wait:q %X waits pri %d", stp->sd_wrq, pri);
	    if (((error = strwaitq(stp, WRITEWAIT, (off_t)0, fmode, &done))
			!= 0) || done) {
			mutex_exit(&stp->sd_lock);
			TRACE_3(TR_FAC_STREAMS_FR, TR_STRPUTMSG_OUT,
				"strputmsg out:q %X out %d error %d",
				stp->sd_wrq, 0, error);
			return (error);
	    }
	    TRACE_1(TR_FAC_STREAMS_FR, TR_STRPUTMSG_WAKE,
			"strputmsg wake:q %X wakes", stp->sd_wrq);
	}
	mutex_exit(&stp->sd_lock);

	uiod.d_iov[0].iov_base = mdata->buf;
	uiod.d_iov[0].iov_len = mdata->len;
	uiod.d_uio.uio_iov = uiod.d_iov;
	uiod.d_uio.uio_iovcnt = 1;
	uiod.d_uio.uio_loffset = 0;
	uiod.d_uio.uio_segflg = UIO_USERSPACE;
	uiod.d_uio.uio_fmode = 0;
	uiod.d_uio.uio_resid = mdata->len;
	if ((error = strmakemsg(mctl, mdata->len, &uiod.d_uio, stp,
	    (long)(flag | (canrw ? STRUIO_POSTPONE : 0)), &mp)) != 0 || !mp) {
		TRACE_3(TR_FAC_STREAMS_FR, TR_STRPUTMSG_OUT,
			"strputmsg out:q %X out %d error %d",
			stp->sd_wrq, 1, error);
		return (error);
	}
	mp->b_band = pri;

	/*
	 * Put message downstream.
	 */
	while (canrw) {
		/*
		 * Stream supports rwnext() for the write side.
		 */
		queue_t *wqp = stp->sd_wrq;

		uiod.d_uio.uio_offset = 0;
		uiod.d_mp = mp;
		if (! (error = rwnext(wqp, &uiod))) {
			if (! uiod.d_mp)
				goto skip;
			ASSERT(mp == uiod.d_mp);
			/*
			 * No takers, so fall-back to putnext().
			 */
			if (! (error = struioget(wqp, mp, &uiod)))
				break;
			goto freeout;
		}
		ASSERT(mp == uiod.d_mp);
		if (error == EINVAL) {
			/*
			 * The stream plumbing must have changed while
			 * we where away, so just turn off rwnext()s.
			 */
			canrw = 0;
			if (! (error = struioget(wqp, mp, &uiod)))
				break;
			goto freeout;
		} else if (error == EBUSY) {
			/*
			 * Couldn't enter a primeter,
			 * so fall-back to putnext().
			 */
			if (! (error = struioget(wqp, mp, &uiod)))
				break;
			goto freeout;
		} else if (error != EWOULDBLOCK)
			goto freeout;
		/*
		 * Didn't write it, most likely due to down-stream
		 * flow control, so just wait for a strwakeq() from
		 * the flow controlled module then try another write.
		 */
		mutex_enter(&stp->sd_lock);
		if (flag & MSG_HIPRI) {
			/*
			 * High priority message, don't wait, use putnext().
			 */
			if (! (error = struioget(wqp, mp, &uiod)))
				break;
			goto freeout;
		}
		TRACE_2(TR_FAC_STREAMS_FR, TR_STRPUTMSG_WAIT,
			"strputmsg wait:q %X waits pri %d", stp->sd_wrq, pri);
		if (((error = strwaitq(stp, WRITEWAIT, (off_t)0, fmode, &done))
		    != 0) || done) {
			mutex_exit(&stp->sd_lock);
			goto freeout;
		}
		TRACE_1(TR_FAC_STREAMS_FR, TR_STRPUTMSG_WAKE,
			"strputmsg wake:q %X wakes", stp->sd_wrq);
		mutex_exit(&stp->sd_lock);
	}
	run_queues = 1;
	putnext(stp->sd_wrq, mp);
skip:;
	queuerun();
out:;
	TRACE_3(TR_FAC_STREAMS_FR, TR_STRPUTMSG_OUT,
		"strputmsg out:q %X out %d error %d", stp->sd_wrq, 2, error);
	return (error);
freeout:
	freemsg(mp);
	TRACE_3(TR_FAC_STREAMS_FR, TR_STRPUTMSG_OUT,
		"strputmsg out:q %X out %d error %d", stp->sd_wrq, 3, error);
	return (error);
}

/*
 * strpoll() QLOCK macro - this macro is used instead of just a mutex_enter()
 * to enter the QLOCK mutex for Q. This is done to prevent an A<>B dead-lock
 * with QLOCK and ps_lock/pplock. strpoll() maybe called with ps_lock/pplock
 * mutex(s) held then tries to enter the QLOCK mutex for Q (A<-B) and another
 * thread has already entered the QLOCK mutex and calls pollwakeup_safe() which
 * tries to enter ps_lock/pplock (A->B).
 *
 * The solution used here is to exit ps_lock/pplock (if need be) and retry the
 * enter of the QLOCK mutext then reenter the ps_lock/pplock (if need be).
 */
#define	STRPOLLQLOCK(STP, Q) if (! mutex_tryenter(QLOCK(Q))) { \
	pollstate_t *ps = curthread->t_pollstate; \
	pollhead_t *php = &STP->sd_pollist; \
	int ps_lock_owned = mutex_owned(&ps->ps_lock); \
	int pp_lock_owned = ps_lock_owned && pplockowned(php); \
 \
	if (pp_lock_owned) \
		ppunlock(php); \
	if (ps_lock_owned) \
		mutex_exit(&ps->ps_lock); \
	mutex_enter(QLOCK(Q)); \
	if (ps_lock_owned) \
		mutex_enter(&ps->ps_lock); \
	if (pp_lock_owned) \
		pplock(php); \
}

/*
 * Determines whether the necessary conditions are set on a stream
 * for it to be readable, writeable, or have exceptions.
 */
int
strpoll(
	register struct stdata *stp,
	short events_arg,
	int anyyet,
	short *reventsp,
	struct pollhead **phpp
)
{
	register int events = (ushort_t)events_arg;
	register int retevents = 0;
	register mblk_t *mp;
	qband_t *qbp;
	long sd_flags = stp->sd_flag;
	int headlocked = 0;

	/*
	 * For performance, a single 'if' tests for most possible edge
	 * conditions in one shot
	 */
	if (sd_flags & (STPLEX | STRDERR | STWRERR)) {
		if (sd_flags & STPLEX) {
			*reventsp = POLLNVAL;
			return (0);
		}
		if (((events & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)) &&
		    (sd_flags & STRDERR)) ||
		    ((events & (POLLOUT | POLLWRNORM | POLLWRBAND)) &&
		    (sd_flags & STWRERR))) {
			*reventsp = POLLERR;
			return (0);
		}
	}
	if (sd_flags & STRHUP) {
		retevents |= POLLHUP;
	} else if (events & (POLLWRNORM | POLLWRBAND)) {
		register queue_t *tq;
		queue_t	*qp = stp->sd_wrq;

		claimstr(qp);
		/* Find next module forward that has a service procedure */
		tq = qp->q_next->q_nfsrv;
		ASSERT(tq != NULL);

		STRPOLLQLOCK(stp, tq);
		if (events & POLLWRNORM) {
			register queue_t *sqp;

			if (tq->q_flag & QFULL)
				/* ensure backq svc procedure runs */
				tq->q_flag |= QWANTW;
			else if ((sqp = stp->sd_struiowrq) != NULL) {
				/* Check sync stream barrier write q */
				mutex_exit(QLOCK(tq));
				STRPOLLQLOCK(stp, sqp);
				if (sqp->q_flag & QFULL)
					/* ensure pollwakeup_safe() is done */
					sqp->q_flag |= QWANTWSYNC;
				else
					retevents |= POLLOUT;
				/* More write events to process ??? */
				if (! (events & POLLWRBAND)) {
					mutex_exit(QLOCK(sqp));
					releasestr(qp);
					goto chkrd;
				}
				mutex_exit(QLOCK(sqp));
				STRPOLLQLOCK(stp, tq);
			} else
				retevents |= POLLOUT;
		}
		if (events & POLLWRBAND) {
			qbp = tq->q_bandp;
			if (qbp) {
				while (qbp) {
					if (qbp->qb_flag & QB_FULL)
						qbp->qb_flag |= QB_WANTW;
					else
						retevents |= POLLWRBAND;
					qbp = qbp->qb_next;
				}
			} else {
				retevents |= POLLWRBAND;
			}
		}
		mutex_exit(QLOCK(tq));
		releasestr(qp);
	}
chkrd:
	if (sd_flags & STRPRI) {
		retevents |= (events & POLLPRI);
	} else if (events & (POLLRDNORM | POLLRDBAND | POLLIN)) {
		queue_t	*qp = RD(stp->sd_wrq);
		int normevents = (events & (POLLIN | POLLRDNORM));

		/*
		 * Note: No need to do the equiv of STRPOLLQLOCK here since
		 * pollwakeup() is never called with stream head lock held.
		 */
		mutex_enter(&stp->sd_lock);
		headlocked = 1;
		mp = qp->q_first;
		while (mp) {
			if (mp->b_band == 0)
				retevents |= normevents;
			else
				retevents |= (events & (POLLIN | POLLRDBAND));

			/*
			 * MSGNOGET is really only to make poll return
			 * the intended events when the module is really
			 * holding onto the data.  Yeah, it's a hack and
			 * we need a better solution.
			 */
			if (mp->b_flag & MSGNOGET)
				mp = mp->b_next;
			else
				break;
		}
		if (! (retevents & normevents) &&
		    (events & normevents) &&
		    (stp->sd_wakeq & RSLEEP)) {
			/* Sync stream barrier read queue has data */
			retevents |= normevents;
		}
	}

	*reventsp = (short)retevents;
	if (retevents) {
		if (headlocked)
			mutex_exit(&stp->sd_lock);
		return (0);
	}

	/*
	 * If poll() has not found any events yet, set up event cell
	 * to wake up the poll if a requested event occurs on this
	 * stream.  Check for collisions with outstanding poll requests.
	 */
	if (!anyyet) {
		*phpp = &stp->sd_pollist;
		if (headlocked == 0) {
			mutex_enter(&stp->sd_lock);
			headlocked = 1;
		}
		stp->sd_flag |= STRPOLL;
	}
	if (headlocked)
		mutex_exit(&stp->sd_lock);
	return (0);
}

/*
 * The purpose of putback() is to assure sleeping polls/reads
 * are awakened when there are no new messages arriving at the,
 * stream head, and a message is placed back on the read queue.
 *
 * sd_lock must be held when messages are placed back on stream
 * head.  (getq() holds sd_lock when it removes messages from
 * the queue)
 */

static void
putback(stp, q, bp, band)
	register struct stdata *stp;
	register queue_t *q;
	register mblk_t *bp;
	register int band;
{

	ASSERT(MUTEX_HELD(&stp->sd_lock));
	(void) putbq(q, bp);
	if (q->q_first == bp) {
		if (stp->sd_flag & RSLEEP) {
			stp->sd_flag &= ~RSLEEP;
			cv_broadcast(&q->q_wait);
		}
		if (stp->sd_flag & STRPRI) {
			mutex_exit(&stp->sd_lock);
			pollwakeup_safe(&stp->sd_pollist, POLLPRI);
		} else {
			if (band == 0) {
				if (stp->sd_flag & STRPOLL) {
					stp->sd_flag &= ~STRPOLL;
					mutex_exit(&stp->sd_lock);
					pollwakeup_safe(&stp->sd_pollist,
						POLLIN | POLLRDNORM);
				} else
					mutex_exit(&stp->sd_lock);
			} else {
				mutex_exit(&stp->sd_lock);
				pollwakeup_safe(&stp->sd_pollist,
					POLLIN | POLLRDBAND);
			}
		}
		mutex_enter(&stp->sd_lock);
	}
}
/*
 * Return the held vnode attached to the stream head of a
 * given queue
 * It is the responsibility of the calling routine to ensure
 * that the queue does not go away (e.g. pop).
 */
vnode_t *
strq2vp(queue_t *qp)
{
	vnode_t *vp;
	vp = STREAM(qp)->sd_vnode;
	ASSERT(vp != NULL);
	VN_HOLD(vp);
	return (vp);
}

/*
 * return the stream head write queue for the given vp
 * It is the responsibility of the calling routine to ensure
 * that the stream or vnode do not close.
 */
queue_t *
strvp2wq(vnode_t *vp)
{
	ASSERT(vp->v_stream != NULL);
	return (vp->v_stream->sd_wrq);
}

/*
 * pollwakeup stream head
 * It is the responsibility of the calling routine to ensure
 * that the stream or vnode do not close.
 */
void
strpollwakeup(vnode_t *vp, short event)
{
	ASSERT(vp->v_stream);
	pollwakeup_safe(&vp->v_stream->sd_pollist, event);
}

/*
 *  No activity allowed on streams
 *  Input: 2 write driver end write queues
 *  Return 0 if error
 *  If wrq1 == wrq2 then we are doing a loop back,
 *  otherwise just mate two queues.
 *  If these queues are not the stream head they must have
 *  a service procedure
 *  It is up to caller to ensure that neither queue goes away.
 *  XXX str_mate() and str_unmate() should be moved to new file kstream.c
 *  XXX these routines need to be a little more general
 */
int
str_mate(queue_t *wrq1, queue_t *wrq2)
{

	/*
	 * Loop back?
	 */
	if (wrq2 == 0 || wrq1 == wrq2) {
		/*
		 * driver end of stream?
		 */
		if (! (wrq1->q_flag & QEND))
			return (EINVAL);

		ASSERT(wrq1->q_next == 0);	/* sanity */

		/*
		 * connect write queue to read queue
		 */
		wrq1->q_next = RD(wrq1);

		/*
		 * If write queue does not have a service routine,
		 * assign the forward service procedure from the
		 * read queue
		 */

		if (! wrq1->q_qinfo->qi_srvp)
			wrq1->q_nfsrv = RD(wrq1)->q_nfsrv;
		/*
		 * set back service procedure..
		 * XXX - note back service procedure is not implemented
		 * this may cause a race condition, breaking it
		 * a bit more.
		 */
		RD(wrq1)->q_nbsrv = wrq1;
	} else {
		/*
		 * driver end of stream?
		 */
		if (! (wrq1->q_flag & QEND))
			return (EINVAL);
		if (! (wrq2->q_flag & QEND))
			return (EINVAL);

		ASSERT(wrq1->q_next == NULL);	/* sanity */
		ASSERT(wrq2->q_next == NULL);	/* sanity */


		/*
		 * if first queue is a stream head, second must
		 * must also be one
		 */
		if (! (RD(wrq1)->q_flag & QEND)) {
			if (RD(wrq2)->q_flag & QEND)
				return (EINVAL);
		} else if (! (RD(wrq2)->q_flag & QEND))
			return (EINVAL);
		/*
		 * Twist the stream head queues so that the write queue
		 * points to the other stream's read queue.
		 */
		wrq1->q_next = RD(wrq2);
		wrq2->q_next = RD(wrq1);

		if (! wrq1->q_qinfo->qi_srvp)
			wrq1->q_nfsrv = RD(wrq2)->q_nfsrv;
		if (! wrq2->q_qinfo->qi_srvp)
			wrq2->q_nfsrv = RD(wrq1)->q_nfsrv;

		/*
		 * Nothing really uses the back service routines,
		 * but fill them in for completeness
		 */

		RD(wrq1)->q_nbsrv = wrq2;
		RD(wrq2)->q_nbsrv = wrq1;

		SETMATED(STREAM(wrq1), STREAM(wrq2));
	}
	return (0);
}
/*
 * XXX There are lot's of problems with this
 * There are several race conditions that must be addressed
 * (e.g. what happens when some other thread is in the other
 * queues q and we unmate.  would freezing the stream be enough?)
 * This routine may only be called from a modules close or service
 * routines.
 */
int
str_unmate(queue_t *wrq1, queue_t *wrq2)
{
	if (! (wrq2->q_flag & QEND) && (wrq1->q_flag & QEND))
		return (EINVAL);

	wrq1->q_next = 0;
	RD(wrq1)->q_nbsrv = NULL;
	wrq1->q_nfsrv = wrq1;

	if (wrq1 == wrq2)
		return (0);

	wrq2->q_next = 0;
	RD(wrq2)->q_nbsrv = NULL;
	wrq2->q_nfsrv = wrq2;

	SETUNMATED(STREAM(wrq1), STREAM(wrq2));
	return (0);
}
/*
 * XXX will go away when console is correctly fixed.
 * Clean up the console PIDS, from previous I_SETSIG,
 * called only for cnopen which never calls strclean().
 */
void
str_cn_clean(vp)
	struct vnode *vp;
{
	strsig_t *ssp, *pssp, *tssp;
	struct stdata *stp;
	struct pid  *pidp;
	int update = 0;

	ASSERT(vp->v_stream);
	stp = vp->v_stream;
	pssp = NULL;
	mutex_enter(&stp->sd_lock);
	ssp = stp->sd_siglist;
	while (ssp) {
		mutex_enter(&pidlock);
		pidp = ssp->ss_pidp;
		/*
		 * Get rid of PID if the proc is gone.
		 */
		if (pidp->pid_prinactive) {
			tssp = ssp->ss_next;
			if (pssp)
				pssp->ss_next = tssp;
			else
				stp->sd_siglist = tssp;
			ASSERT(pidp->pid_ref <= 1);
			PID_RELE(ssp->ss_pidp);
			mutex_exit(&pidlock);
			kmem_cache_free(strsig_cache, ssp);
			update = 1;
			ssp = tssp;
			continue;
		} else
			mutex_exit(&pidlock);
		pssp = ssp;
		ssp = ssp->ss_next;
	}
	if (update) {
		stp->sd_sigflags = 0;
		for (ssp = stp->sd_siglist; ssp; ssp = ssp->ss_next)
			stp->sd_sigflags |= ssp->ss_events;
	}
	mutex_exit(&stp->sd_lock);
}
