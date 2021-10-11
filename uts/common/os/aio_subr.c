/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)aio_subr.c 1.12     95/07/25 SMI"
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <vm/as.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/aio_impl.h>

int aphysio(int (*)(), int (*)(), dev_t, int, void (*)(), struct aio_req *);
void aphysio_cleanup(aio_req_t *);
void aio_cleanup(void);
void aio_copyout_result(aio_req_t *);
void aio_cleanup_exit(void);

/*
 * private functions
 */
static void aio_done(struct buf *);

int
aphysio(
	int (*strategy)(),
	int (*cancel)(),
	dev_t dev,
	int rw,
	void (*mincnt)(),
	struct aio_req *aio)
{
	struct uio *uio = aio->aio_uio;
	aio_req_t *aio_reqp = (aio_req_t *)aio->aio_private;
	struct buf *bp = &aio_reqp->aio_req_buf;
	struct iovec *iov;
	struct as *as;
	faultcode_t fault_err;
	char *a;
	int c, error;

	if (uio->uio_loffset < 0 || uio->uio_loffset > MAXOFFSET_T) {
		return (EINVAL);
	}

	iov = uio->uio_iov;
	sema_init(&bp->b_sem, 0, "bp owner", SEMA_DEFAULT, DEFAULT_WT);
	sema_init(&bp->b_io, 0, "bp io", SEMA_DEFAULT, DEFAULT_WT);

	bp->b_oerror = 0;		/* old error field */
	bp->b_error = 0;
	bp->b_iodone = (int (*)()) aio_done;
	bp->b_flags = B_KERNBUF | B_BUSY | B_PHYS | B_ASYNC | rw;
	bp->b_edev = dev;
	bp->b_dev = cmpdev(dev);
	bp->b_lblkno = btodt(uio->uio_loffset);
	/* b_forw points at an aio_req_t structure */
	bp->b_forw = (struct buf *)aio_reqp;

	a = bp->b_un.b_addr = iov->iov_base;
	c = bp->b_bcount = iov->iov_len;

	(*mincnt)(bp);
	if (bp->b_bcount != iov->iov_len)
		return (ENOTSUP);

	bp->b_proc = curproc;
	as = curproc->p_as;

	fault_err = as_fault(as->a_hat, as, a, (uint)c,
			F_SOFTLOCK, rw == B_READ? S_WRITE : S_READ);

	if (fault_err != 0) {
		/*
		 * Even though the range of addresses were
		 * valid and had the correct permissions,
		 * we couldn't lock down all the pages for
		 * the access we needed. (e.g. we needed to
		 * allocate filesystem blocks for
		 * rw == B_READ but the file system was full).
		 */
		switch (FC_CODE(fault_err)) {
		case FC_OBJERR:
			error = FC_ERRNO(fault_err);
			break;
		case FC_PROT:
			error = EACCES;
			break;
		default:
			error = EFAULT;
			break;
		}

		bp->b_flags |= B_ERROR;
		bp->b_error = error;
		bp->b_flags &= ~(B_BUSY|B_WANTED|B_PHYS);
		return (error);
	}

	if (cancel != anocancel)
		panic("aphysio(): cancellation not supported, use anocancel");

	aio_reqp->aio_req_cancel = cancel;
	return ((*strategy)(bp));
}

/*ARGSUSED*/
int
anocancel(struct buf *bp)
{
	return (ENXIO);
}

/*
 * Called from biodone().
 * Notify process that a pending AIO has finished.
 */
static void
aio_done(struct buf *bp)
{
	proc_t *p;
	struct as *as;
	aio_req_t *aio_reqp;
	aio_t *aiop;
	int fd;
	int pollqflags;
	void (*func)();

	p = bp->b_proc;
	/*
	 * Put the request on the per-process doneq or the
	 * per-process pollq depending on the requests flags.
	 */
	aio_reqp = (aio_req_t *)bp->b_forw;
	fd = aio_reqp->aio_req_fd;
	aiop = p->p_aio;
	ASSERT(aiop != NULL);
	aio_reqp->aio_req_next =  NULL;
	mutex_enter(&aiop->aio_mutex);
	ASSERT(aiop->aio_pending >= 0);
	pollqflags = ((aiop->aio_flags & AIO_CLEANUP) |
			    (aio_reqp->aio_req_flags & AIO_POLL));
	aiop->aio_pending--;
	if (pollqflags) {
		/* put request on the poll queue. */
		if (pollqflags & AIO_CLEANUP) {
			as = p->p_as;
			mutex_enter(&as->a_contents);
		}
		if (aiop->aio_pollq.head == NULL) {
			aiop->aio_pollq.head = aio_reqp;
			aiop->aio_pollq.tail = aio_reqp;
		} else {
			aiop->aio_pollq.tail->aio_req_next = aio_reqp;
			aiop->aio_pollq.tail = aio_reqp;
		}
		if (pollqflags & AIO_CLEANUP) {
			mutex_exit(&aiop->aio_mutex);
			cv_signal(&aiop->aio_cleanupcv);
			mutex_exit(&as->a_contents);
		} else {
			/*
			 * let the pollq processing happen from an
			 * AST. set an AST on all threads in this process
			 * and wakeup anybody waiting in aiowait().
			 */
			cv_broadcast(&aiop->aio_waitcv);
			mutex_exit(&aiop->aio_mutex);
			mutex_enter(&p->p_lock);
			set_proc_ast(p);
			mutex_exit(&p->p_lock);
		}
	} else {
		/* put request on done queue. */
		if (aiop->aio_doneq.head == NULL) {
			aiop->aio_doneq.head = aio_reqp;
			aiop->aio_doneq.tail = aio_reqp;
		} else {
			aiop->aio_doneq.tail->aio_req_next = aio_reqp;
			aiop->aio_doneq.tail = aio_reqp;
		}
		ASSERT(aiop->aio_pending >= 0);
		cv_broadcast(&aiop->aio_waitcv);
		mutex_exit(&aiop->aio_mutex);
		/*
		 * Send a SIGIO signal to the process if a signal handler
		 * is installed.
		*/
		if ((func = p->p_user.u_signal[SIGIO - 1]) != SIG_DFL &&
		    func != SIG_IGN)
			psignal(p, SIGIO);
	}
	/* release fd, now that the pending request completed. */
	areleasef(fd, p);
}

/*
 * cleanup after aphysio(). aio request was SOFTLOCKed by aphysio()
 * and needs to be SOFTUNLOCKed.
 */
void
aphysio_cleanup(aio_req_t *aio_reqp)
{
	proc_t *p;
	struct as *as;
	struct buf *bp;
	struct iovec *iov;
	int flags, error;

	if (aio_reqp->aio_req_flags & AIO_DONE)
		return;

	if ((aio_reqp->aio_req_flags & AIO_POLL) == 0)
		aio_copyout_result(aio_reqp);

	p = curproc;
	as = p->p_as;
	bp = &aio_reqp->aio_req_buf;
	iov = aio_reqp->aio_req_uio.uio_iov;
	flags = (((bp->b_flags & B_READ) == B_READ) ? S_WRITE : S_READ);
	error = as_fault(as->a_hat, as, iov->iov_base,
		    (uint)iov->iov_len, F_SOFTUNLOCK, flags);
	if (error) {
		switch (FC_CODE(error)) {
		case FC_OBJERR:
			error = FC_ERRNO(error);
			break;
		case FC_PROT:
			error = EACCES;
			break;
		default:
			error = EFAULT;
			break;
		}
		cmn_err(CE_PANIC, "physio unlock %d", error);
	}
	aio_reqp->aio_req_flags |= AIO_DONE;
	bp->b_flags &= ~(B_BUSY|B_WANTED|B_PHYS);
	bp->b_flags |= B_DONE;
	if (bp->b_flags & B_REMAPPED)
		bp_mapout(bp);
}

/*
 * cleanup aio requests that are on the per-process poll queue.
 */
void
aio_cleanup()
{
	aio_t *aiop = curproc->p_aio;
	aio_req_t *aio_reqp, *next, *first, *last;
	int exitflg;
	void (*func)();

	ASSERT(aiop != NULL);

	if (aiop->aio_pollq.head == NULL)
		return;

	/*
	 * take all the requests off the poll queue.
	 */
	mutex_enter(&aiop->aio_mutex);
	next = aiop->aio_pollq.head;
	first = next;
	if (first != NULL)
		aiop->aio_pollq.head = aiop->aio_pollq.tail = NULL;
	mutex_exit(&aiop->aio_mutex);

	/*
	 * return immediately if poll queue is empty.
	 * someone else must have already emptied it.
	 */
	if (first == NULL)
		return;

	for (; (aio_reqp = next) != NULL; last = next, next =
	    aio_reqp->aio_req_next) {
		/* skip if AIO_POLL flag is not set for this AIO request. */
		if ((aio_reqp->aio_req_flags & AIO_POLL) == 0)
			continue;
		/* copy out results to user-level result_t */
		aio_copyout_result(aio_reqp);
	}
	/*
	 * when AIO_CLEANUP is set, aphysio_cleanup() is called to
	 * cleanup after each request. if the process is terminating,
	 * the requests should be discarded and should not be put on
	 * the done queue.
	 */
	exitflg = (curproc->p_flag & EXITLWPS);
	if (aiop->aio_flags & AIO_CLEANUP) {
		next = first;
		while ((aio_reqp = next) != NULL) {
			next = aio_reqp->aio_req_next;
			aphysio_cleanup(aio_reqp);
			if (exitflg)
				kmem_free(aio_reqp, sizeof (struct aio_req_t));
		}
		if (exitflg)
			return;
	}
	/*
	 * put requests on the aiop's done queue.
	 */
	mutex_enter(&aiop->aio_mutex);
	if (aiop->aio_doneq.head == NULL) {
		aiop->aio_doneq.head = first;
		aiop->aio_doneq.tail = last;
	} else {
		aiop->aio_doneq.tail->aio_req_next = first;
		aiop->aio_doneq.tail = last;
	}
	/* wakeup everybody in this process blocked in aiowait() */
	cv_broadcast(&aiop->aio_waitcv);
	mutex_exit(&aiop->aio_mutex);
	/*
	 * Send a SIGIO signal to this process if a signal handler
	 * is installed.
	 */
	if ((func = curproc->p_user.u_signal[SIGIO - 1]) != SIG_DFL &&
	    func != SIG_IGN) {
		psignal(curproc, SIGIO);
	}
}

/*
 * called by exit(). waits for all outstanding kaio to finish
 * before the kaio resources are freed.
 */
void
aio_cleanup_exit()
{
	proc_t *p = curproc;
	aio_t *aiop = p->p_aio;
	aio_req_t *aio_reqp, *next;

	/*
	 * wait for all outstanding kaio to complete. process
	 * is now single-threaded; no other kaio requests can
	 * happen once aio_pending is zero.
	 */
	mutex_enter(&aiop->aio_mutex);
	aiop->aio_flags |= AIO_CLEANUP;
	while (aiop->aio_pending != 0)
		cv_wait(&aiop->aio_cleanupcv, &aiop->aio_mutex);
	mutex_exit(&aiop->aio_mutex);
	/*
	 * cleanup poll queue. EXITLWPS is temporarily set so
	 * that aio_cleanup() knows that the process is exiting.
	 */

	/*
	 * This really should be done by passing an argument to aio_cleanup().
	 */
	mutex_enter(&p->p_lock);
	p->p_flag |= EXITLWPS;
	mutex_exit(&p->p_lock);

	aio_cleanup();

	mutex_enter(&p->p_lock);
	p->p_flag &= ~EXITLWPS;
	mutex_exit(&p->p_lock);
	/*
	 * free up the done queues resources.
	 */
	next = aiop->aio_doneq.head;
	while ((aio_reqp = next) != NULL) {
		next = aio_reqp->aio_req_next;
		aphysio_cleanup(aio_reqp);
		kmem_free(aio_reqp, sizeof (struct aio_req_t));
	}
	/*
	 * release the freelist.
	 */
	next = aiop->aio_free;
	while ((aio_reqp = next) != NULL) {
		next = aio_reqp->aio_req_next;
		kmem_free(aio_reqp, sizeof (struct aio_req_t));
	}
	mutex_destroy(&aiop->aio_mutex);
	kmem_free(p->p_aio, sizeof (struct aio));
}

/*
 * copy out aio request's result to a user-level result_t buffer.
 */
void
aio_copyout_result(aio_req_t *aio_reqp)
{
	struct buf *bp;
	struct iovec *iov;
	int flags, error;
	aio_result_t *resultp;
	int errno;
	int retval;

	iov = aio_reqp->aio_req_uio.uio_iov;
	bp = &aio_reqp->aio_req_buf;
	/* "resulp" points to user-level result_t buffer */
	resultp = aio_reqp->aio_req_resultp;
	if (bp->b_flags & B_ERROR) {
		if (bp->b_error)
			errno = bp->b_error;
		else
			errno = EIO;
		(void) suword((int *)(&resultp->aio_errno),
			    errno);
		retval = -1;
	} else {
		/* AIO was successful, errno should already be zero */
		retval = iov->iov_len - bp->b_resid;
	}
	(void) suword((int *)(&resultp->aio_return), retval);
}
