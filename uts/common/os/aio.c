/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)aio.c 1.12     95/05/22 SMI"

/*
 * Kernel asynchronous I/O.
 * This is only for raw devices now (as of Nov. 1993).
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/fs/snode.h>
#include <sys/unistd.h>
#include <sys/cmn_err.h>
#include <vm/faultcode.h>
#include <sys/sysmacros.h>
#include <sys/procfs.h>
#include <sys/kmem.h>
#include <sys/autoconf.h>
#include <sys/ddi_impldefs.h>
#include <sys/sunddi.h>
#include <sys/aio_impl.h>
#include <sys/debug.h>

struct arwa {
	int 	opcode;			/* aio call type */
	int 	fdes;			/* file descriptor */
	char 	*bufp;			/* buffer pointer */
	int 	bufsize;		/* buffer size */
	offset_t offset;		/* offset */
	aio_result_t *resultp; 		/* result pointer */
};

struct aioa {
	int	opcode;
};

struct aiowaita {
	int	opcode;
	struct timeval *timeout;	/* timeout struct */
	int flag;
};

extern  clock_t lbolt;
extern	int freemem;
extern	int desfree;

/*
 * external entry point.
 */
static int kaio(struct aioa *, rval_t *);

/*
 * implementation specific functions
 */
static int arw(struct arwa *, int);
static int aiowait(struct aiowaita *, rval_t *);
static int aionotify(void);
static int aioinit(void);
static int aiostart(void);
static int (*check_vp(struct vnode *, int))(void);
static aio_t *aio_aiop_alloc();
static int aio_req_alloc(aio_req_t **, aio_result_t *);
static int aio_hash_insert(struct aio_req_t *, aio_t *);
static void aio_hash_delete(struct aio_req_t *, struct aio *);
static int aio_req_setup(aio_req_t **, aio_t *, struct arwa *);
static void aio_req_free(aio_req_t *, aio_t *);
static int aio_cleanup_thread(aio_t *);

#define	AIO_HASH(resultp)	(((unsigned)(resultp) >> 3) & (AIO_HASHSZ-1))
#define	DUPLICATE 1
/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>
#include <sys/syscall.h>


static struct sysent kaio_sysent = {
	7,
	SE_NOUNLOAD,			/* not unloadable once loaded */
	kaio
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_syscallops;

static struct modlsys modlsys = {
	&mod_syscallops,
	"kernel Async I/O",
	&kaio_sysent
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlsys, NULL
};

_init()
{
	int retval;

	if ((retval = mod_install(&modlinkage)) != 0)
		return (retval);

	return (0);
}

_fini()
{
	register int retval;

	retval = mod_remove(&modlinkage);

	return (retval);
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

static int
kaio(
	register struct aioa *uap,
	rval_t *rvp)
{
	switch (uap->opcode & ~AIO_POLL_BIT) {
	case AIOREAD:
		return (arw((struct arwa *)uap, FREAD));
	case AIOWRITE:
		return (arw((struct arwa *)uap, FWRITE));
	case AIOWAIT:
		return (aiowait((struct aiowaita *)uap, rvp));
	case AIONOTIFY:
		return (aionotify());
	case AIOINIT:
		return (aioinit());
	case AIOSTART:
		return (aiostart());
	default:
		return (EINVAL);
	}
}

/*
 * wake up LWPs in this process that are sleeping in
 * aiowait().
 */
static int
aionotify(void)
{
	aio_t	*aiop;

	aiop = curproc->p_aio;
	if (aiop == NULL)
		return (0);

	mutex_enter(&aiop->aio_mutex);
	aiop->aio_notifycnt++;
	cv_broadcast(&aiop->aio_waitcv);
	mutex_exit(&aiop->aio_mutex);

	return (0);
}

static int
aiowait(
	struct aiowaita *uap,
	rval_t *rvp)
{
	int 		error = 0;
	aio_t		*aiop;
	aio_req_t	*aio_reqp;
	struct timeval 	wait_time, now;
	long		ticks = 0;
	int 		status;
	int		blocking;

	aiop = curproc->p_aio;
	if (aiop == NULL)
		return (EINVAL);

	/*
	 * first copy the timeval struct into the kernel.
	 * if the caller is polling, the caller will not
	 * block and "blocking" should be zero.
	 */
	if (uap->timeout) {
		if ((int)uap->timeout == -1)
			blocking = 0;
		else {
			if (copyin((caddr_t)uap->timeout, (caddr_t)&wait_time,
			    sizeof (wait_time)))
				return (EFAULT);

			if (wait_time.tv_sec > 0 || wait_time.tv_usec > 0) {
				if (error = itimerfix(&wait_time)) {
					return (error);
				}
				blocking = 1;
				uniqtime(&now);
				timevaladd(&wait_time, &now);
				ticks = hzto(&wait_time);
				ticks += lbolt;
			} else
				blocking = 0;
		}
	} else
		blocking = 1;

	mutex_enter(&aiop->aio_mutex);
	/* don't block if there is no outstanding aio */
	if (aiop->aio_outstanding == 0 && uap->flag == -1) {
		if (aiop->aio_notifycnt > 0) {
			aiop->aio_notifycnt--;
			rvp->r_val1 = 1;
			error = 0;
		} else
			error = EINVAL;
		mutex_exit(&aiop->aio_mutex);
		return (error);
	}
	/*
	 * any requests on the poll queue should be processed and
	 * put on the done queue.
	 */
	if (aiop->aio_pollq.head) {
		mutex_exit(&aiop->aio_mutex);
		aio_cleanup();
		mutex_enter(&aiop->aio_mutex);
	}
	for (;;) {
		if ((aio_reqp = aiop->aio_doneq.head) != NULL) {
			/*
			 * remove aio_reqp from the done list and
			 * place it on the free list.
			 */
			aiop->aio_doneq.head = aio_reqp->aio_req_next;
			rvp->r_val1 = (int)aio_reqp->aio_req_resultp;
		} else if (aiop->aio_notifycnt > 0) {
			/*
			 * nothing on the kernel's queue. the user
			 * has notified the kernel that it has items
			 * on a user-level queue.
			 */
			aiop->aio_notifycnt--;
			rvp->r_val1 = 1;
		} else if (blocking) {
			if (ticks)
				status = cv_timedwait_sig(&aiop->aio_waitcv,
				    &aiop->aio_mutex, ticks);
			else
				status = cv_wait_sig(&aiop->aio_waitcv,
					    &aiop->aio_mutex);

			if (status > 0)  {
				/* process requests on poll queue */
				if (aiop->aio_pollq.head) {
					mutex_exit(&aiop->aio_mutex);
					aio_cleanup();
					mutex_enter(&aiop->aio_mutex);
				}
				/*
				 * first check if there is anything on
				 * the kernel's done queue, then check
				 * if the user has notified the kernel
				 * about having items on its user-level
				 * queue.
				 */
				if (aiop->aio_doneq.head)
					continue;
				else {
					if (aiop->aio_notifycnt > 0)
						aiop->aio_notifycnt--;
					rvp->r_val1 = 1;
				}
			} else if (status == 0) {
				/*
				 * signal has been delivered.
				 */
				error = EINTR;
			} else if (status == -1) {
				/*
				 * timer expired.
				 */
				error = ETIME;
			}
		}
		break;
	}
	mutex_exit(&aiop->aio_mutex);
	if (aio_reqp) {
		aphysio_cleanup(aio_reqp);
		mutex_enter(&aiop->aio_mutex);
		aio_req_free(aio_reqp, aiop);
		mutex_exit(&aiop->aio_mutex);
	}
	return (error);
}

/*
 * initialize aio by allocating an aio_t struct for this
 * process.
 */
static int
aioinit(void)
{
	proc_t *p = curproc;
	aio_t *aiop = p->p_aio;
	mutex_enter(&p->p_lock);
	if (aiop == NULL)
		aiop = aio_aiop_alloc();
	mutex_exit(&p->p_lock);
	if ((int)aiop == -1)
		return (ENOMEM);
	else
		return (0);
}

/*
 * start a special thread that will cleanup after aio requests
 * that are preventing a segment from being unmapped. as_unmap()
 * blocks until all phsyio to this segment is completed. this
 * doesn't happen until all the pages in this segment are not
 * SOFTLOCKed. Some pages will be SOFTLOCKed when there are aio
 * requests still outstanding. this special thread will make sure
 * that these SOFTLOCKed pages will eventually be SOFTUNLOCKed.
 *
 * this function will return an error if the process has only
 * one LWP. the assumption is that the caller is a separate LWP
 * that remains blocked in the kernel for the life of this process.
 */
static int
aiostart()
{
	proc_t *p = curproc;
	aio_t *aiop;
	int first, error = 0;

	if (p->p_lwpcnt == 1)
		return (EDEADLK);
	mutex_enter(&p->p_lock);
	if ((aiop = p->p_aio) == NULL)
		error = EINVAL;
	else {
		first = aiop->aio_ok;
		if (aiop->aio_ok == 0)
			aiop->aio_ok = 1;
	}
	mutex_exit(&p->p_lock);
	if (error == 0 && first == 0) {
		return (aio_cleanup_thread(aiop));
		/* should never return */
	}
	return (error);
}

/*
 * asynchronous read and write
 */
static int
arw(
	struct arwa 	*uap,
	int		mode)
{
	file_t		*fp;
	int		error;
	struct vnode	*vp;
	aio_req_t	*aio_reqp;
	aio_t		*aiop;
	int		(*aio_func)();

	aiop = curproc->p_aio;
	if (aiop == NULL)
		return (EINVAL);

	if ((fp = getf(uap->fdes)) == NULL) {
		return (EBADF);
	}

	/*
	 * check the permission of the partition
	 */
	if ((fp->f_flag & mode) == 0) {
		releasef(uap->fdes);
		return (EBADF);
	}

	vp = fp->f_vnode;
	aio_func = check_vp(vp, mode);
	if (aio_func == NULL) {
		releasef(uap->fdes);
		return (ENOTSUP);
	}

	error = aio_req_setup(&aio_reqp, aiop, uap);
	if (error) {
		releasef(uap->fdes);
		return (error);
	}

	/*
	 * send the request to driver.
	 */
	error = (*aio_func)((VTOS(vp))->s_dev,
			(struct aio_req *)&aio_reqp->aio_req, CRED());
	/*
	 * the fd is stored in the aio_req_t by aio_req_setup(), and
	 * is released by the aio_cleanup_thread() when the IO has
	 * completed.
	 */
	if (error) {
		releasef(uap->fdes);
		mutex_enter(&aiop->aio_mutex);
		aio_req_free(aio_reqp, aiop);
		aiop->aio_pending--;
		mutex_exit(&aiop->aio_mutex);
	}
	return (error);
}

static int
aio_req_setup(
	aio_req_t **aio_reqp,
	aio_t *aiop,
	struct arwa *uap)
{
	aio_req_t *reqp;
	struct uio *uio;
	int error;

	mutex_enter(&aiop->aio_mutex);
	/*
	 * get an aio_reqp from the free list or allocate one
	 * from dynamic memory.
	 */
	if (error = aio_req_alloc(&reqp, uap->resultp)) {
		mutex_exit(&aiop->aio_mutex);
		return (error);
	}
	aiop->aio_pending++;
	aiop->aio_outstanding++;
	mutex_exit(&aiop->aio_mutex);
	/*
	 * initialize aio request.
	 */
	if (uap->opcode & AIO_POLL_BIT)
		reqp->aio_req_flags = AIO_POLL;
	else
		reqp->aio_req_flags = 0;
	reqp->aio_req_fd = uap->fdes;
	uio = reqp->aio_req.aio_uio;
	uio->uio_iovcnt = 1;
	uio->uio_iov->iov_base = uap->bufp;
	uio->uio_iov->iov_len = uap->bufsize;
	uio->uio_loffset = uap->offset;
	*aio_reqp = reqp;
	return (0);
}

/*
 * Allocate p_aio struct.
 */
static aio_t *
aio_aiop_alloc()
{
	aio_t	*aiop;
	char name[32];

	ASSERT(MUTEX_HELD(&curproc->p_lock));
	aiop = kmem_zalloc(sizeof (struct aio), KM_NOSLEEP);
	if (aiop) {
		curproc->p_aio = aiop;
		sprintf(name, "aio done mutex %8d",
			(int) curproc->p_pidp->pid_id);
		mutex_init(&aiop->aio_mutex, name, MUTEX_DEFAULT,
			DEFAULT_WT);
		return (aiop);
	} else
		return (NULL);
}

/*
 * Allocate an aio_req struct.
 */
static int
aio_req_alloc(aio_req_t **aio_reqp, aio_result_t *resultp)
{
	aio_req_t *reqp;
	aio_t *aiop = curproc->p_aio;

	ASSERT(MUTEX_HELD(&aiop->aio_mutex));

	if ((reqp = aiop->aio_free) != NULL) {
		reqp->aio_req_flags = 0;
		aiop->aio_free = reqp->aio_req_next;
	} else {
		/*
		 * Check whether memory is getting tight.
		 * This is a temporary mechanism to avoid memory
		 * exhaustion by a single process until we come up
		 * with a per process solution such as setrlimit().
		 */
		if (freemem < desfree)
			return (EAGAIN);

		reqp = kmem_zalloc(sizeof (struct aio_req_t), KM_NOSLEEP);
		if (reqp == NULL)
			return (EAGAIN);
		reqp->aio_req.aio_uio = &(reqp->aio_req_uio);
		reqp->aio_req.aio_uio->uio_iov = &(reqp->aio_req_iov);
		reqp->aio_req.aio_private = reqp;
	}

	reqp->aio_req_resultp = resultp;
	if (aio_hash_insert(reqp, aiop)) {
		reqp->aio_req_next = aiop->aio_free;
		aiop->aio_free = reqp;
		return (EINVAL);
	}
	*aio_reqp = reqp;
	return (0);
}

/*
 * Put an aio_reqp onto the freelist.
 */
static void
aio_req_free(
	aio_req_t *aio_reqp,
	aio_t *aiop)
{
	ASSERT(MUTEX_HELD(&aiop->aio_mutex));

	aio_reqp->aio_req_next = aiop->aio_free;
	aiop->aio_free = aio_reqp;
	aiop->aio_outstanding--;
	aio_hash_delete(aio_reqp, aiop);
}

/*
 * this is a special per-process thread whose sole responsibility is
 * to cleanup after asynch IO from a segment that is being unmapped by
 * as_unmap().
 */
static int
aio_cleanup_thread(aio_t *aiop)
{
	proc_t *p = curproc;
	kthread_t *t = curthread;
	struct as *as = p->p_as;
	int poked = 0;
	kcondvar_t *cvp;

	sigfillset(&curthread->t_hold);
	for (;;) {
		/*
		 * If a_unmapwait is set, empty out the done queue and move its
		 * contents to the poll queue. Otherwise, just flush the poll
		 * queue because aio_cleanup_thread can be waken up by external
		 * events such as truss or pokelwp.
		 * AIO_CLEANUP is set to force aio_cleanup()
		 * to SOFTUNLOCK each request via aphysio_cleanup().
		 */
		mutex_enter(&aiop->aio_mutex);
		if (as->a_unmapwait && aiop->aio_doneq.head) {
			aiop->aio_doneq.tail->aio_req_next =
				aiop->aio_pollq.head;
			aiop->aio_pollq.head = aiop->aio_doneq.head;
			if (aiop->aio_pollq.tail == NULL)
				aiop->aio_pollq.tail = aiop->aio_doneq.tail;
			aiop->aio_doneq.head = NULL;
			aiop->aio_doneq.tail = NULL;
		}
		aiop->aio_flags |= AIO_CLEANUP;
		mutex_exit(&aiop->aio_mutex);
		aio_cleanup();
		/*
		 * thread should block on the cleanupcv while
		 * AIO_CLEANUP is set.
		 */
		cvp = &aiop->aio_cleanupcv;
		mutex_enter(&aiop->aio_mutex);
		mutex_enter(&as->a_contents);
		if (aiop->aio_pollq.head == NULL) {
			/*
			 * AIO_CLEANUP determines when the cleanup thread
			 * should be active. this flag is only set when
			 * the cleanup thread is awakened by as_unmap().
			 * the flag is cleared when the blocking as_unmap()
			 * that originally awakened us is allowed to
			 * complete. as_unmap() blocks when trying to
			 * unmap a segment that has SOFTLOCKed pages. when
			 * the segment's pages are all SOFTUNLOCKed,
			 * as->a_unmapwait should be zero. The flag shouldn't
			 * be cleared right away if the cleanup thread
			 * was interrupted because the process is forking.
			 * this happens when cv_wait_sig() returns zero,
			 * because it was awakened by a pokelwps(), if
			 * the process is not exiting, it must be forking.
			 */
			if (as->a_unmapwait == 0 && !poked) {
				aiop->aio_flags &= ~AIO_CLEANUP;
				cvp = &as->a_cv;
			}
			mutex_exit(&aiop->aio_mutex);
			if (poked) {
				if (aiop->aio_pending == 0) {
					if (p->p_flag & EXITLWPS)
						break;
					else if (p->p_flag &
					    (HOLDLWPS|HOLDLWP2)) {
						/*
						 * hold LWP until it
						 * is continued.
						 */
						mutex_exit(&as->a_contents);
						mutex_enter(&p->p_lock);
						stop(t, PR_SUSPENDED, 0);
						mutex_exit(&p->p_lock);
						poked = 0;
						continue;
					}
				}
				cv_wait(cvp, &as->a_contents);
			} else {
				poked = !cv_wait_sig(cvp, &as->a_contents);
			}
		} else
			mutex_exit(&aiop->aio_mutex);

		mutex_exit(&as->a_contents);
	}
exit:
	mutex_exit(&as->a_contents);
	ASSERT((curproc->p_flag & EXITLWPS));
	return (0);
}

static int
aio_hash_insert(
	aio_req_t *aio_reqp,
	aio_t *aiop)
{
	int index;
	aio_result_t *resultp = aio_reqp->aio_req_resultp;
	struct aio_req_t *current;
	struct aio_req_t **nextp;

	index = AIO_HASH(resultp);
	nextp = (aiop->aio_hash + index);
	while ((current = *nextp) != NULL) {
		if (current->aio_req_resultp == resultp)
			return (DUPLICATE);
		nextp = &current->aio_hash_next;
	}
	*nextp = aio_reqp;
	aio_reqp->aio_hash_next = NULL;
	return (0);
}

static void
aio_hash_delete(
	struct aio_req_t *aio_reqp,
	aio_t *aiop)
{
	int index;
	aio_result_t *resultp = aio_reqp->aio_req_resultp;
	aio_req_t *current;
	aio_req_t **nextp;

	index = AIO_HASH(resultp);
	nextp = (aiop->aio_hash + index);
	while ((current = *nextp) != NULL) {
		if (current->aio_req_resultp == resultp) {
			*nextp = current->aio_hash_next;
			return;
		}
		nextp = &current->aio_hash_next;
	}
}

static int
(*check_vp(struct vnode *vp, int mode))(void)
{

	struct snode    *sp = VTOS(vp);
	dev_t		dev = sp->s_dev;
	struct cb_ops  	*cb;
	major_t		major;
	int		(*aio_func)();

	major = getmajor(dev);

	/*
	 * return NULL for requests to files and STREAMs so
	 * that libaio takes care of them.
	 */
	if (vp->v_type == VCHR) {
		/* no stream device for kaio */
		if (STREAMSTAB(major)) {
			return (NULL);
		}
	} else {
		return (NULL);
	}

	/*
	 * Check old drivers which do not have async I/O entry points.
	 */
	if (devopsp[major]->devo_rev < 3)
		return (NULL);

	cb = devopsp[major]->devo_cb_ops;

	/*
	 * No support for mt-unsafe drivers.
	 */
	if (!(cb->cb_flag & D_MP))
		return (NULL);

	if (cb->cb_rev < 1)
		return (NULL);

	/*
	 * Check whether this device is a block device.
	 * Kaio is not supported for devices like tty.
	 */
	if (cb->cb_strategy == nodev || cb->cb_strategy == NULL)
		return (NULL);

	if (mode & FREAD)
		aio_func = cb->cb_aread;
	else
		aio_func = cb->cb_awrite;

	/*
	 * Do we need this ?
	 * nodev returns ENXIO anyway.
	 */
	if (aio_func == nodev)
		return (NULL);

	smark(sp, SACC);
	return (aio_func);
}
