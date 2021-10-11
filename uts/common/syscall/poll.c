/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *			All rights reserved.
 *
 */

#ident	"@(#)poll.c	1.85	95/01/13 SMI"	/* SVr4 1.103	*/
/*
 * System call routines for operations on files.  These manipulate
 * the global and per-process file table entries which refer to
 * vnodes, the system generic file abstraction.
 *
 * Many operations take a path name.  After preparing arguments, a
 * typical operation may proceed with:
 *
 *	error = lookupname(name, seg, followlink, &dvp, &vp);
 *
 * where "name" is the path name operated on, "seg" is UIO_USERSPACE
 * or UIO_SYSSPACE to indicate the address space in which the path
 * name resides, "followlink" specifies whether to follow symbolic
 * links, "dvp" is a pointer to a vnode for the directory containing
 * "name", and "vp" is a pointer to a vnode for "name".  (Both "dvp"
 * and "vp" are filled in by lookupname()).  "error" is zero for a
 * successful lookup, or a non-zero errno (from <sys/errno.h>) if an
 * error occurred.  This paradigm, in which routines return error
 * numbers to their callers and other information is returned via
 * reference parameters, now appears in many places in the kernel.
 *
 * lookupname() fetches the path name string into an internal buffer
 * using pn_get() (pathname.c) and extracts each component of the path
 * by iterative application of the file system-specific VOP_LOOKUP
 * operation until the final vnode and/or its parent are found.
 * (There is provision for multiple-component lookup as well.)  If
 * either of the addresses for dvp or vp are NULL, lookupname() assumes
 * that the caller is not interested in that vnode.  Once a vnode has
 * been found, a vnode operation (e.g. VOP_OPEN, VOP_READ) may be
 * applied to it.
 *
 * With few exceptions (made only for reasons of backward compatibility)
 * operations on vnodes are atomic, so that in general vnodes are not
 * locked at this level, and vnode locking occurs at lower levels (either
 * locally, or, perhaps, on a remote machine.  (The exceptions make use
 * of the VOP_RWLOCK and VOP_RWUNLOCK operations, and include VOP_READ,
 * VOP_WRITE, and VOP_READDIR).  In addition permission checking is
 * generally done by the specific filesystem, via its VOP_ACCESS
 * operation.  The upper (vnode) layer performs checks involving file
 * types (e.g. VREG, VDIR), since the type is static over the life of
 * the vnode.
 */

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

static void ppwakeupemptylist(pollhead_t *php);

/*
 * Poll file descriptors for interesting events.
 */

struct polla {
	pollfd_t *fdp;
	unsigned long nfds;
	long	timo;
};

int
poll(uap, rvp)
	struct polla *uap;
	rval_t *rvp;
{
	int i, fdcnt = 0;
	long msec_timo;
	time_t start;
	int nfds;
	int id;
	pollfd_t *pollfdp;
	polldat_t *pdp;
	pollstate_t *ps;
	int error = 0;
	int lwptotal;
	struct proc *p = curproc;
	register struct user *up = PTOU(p);

	start = lbolt;
	nfds = uap->nfds;
	if (nfds < 0 || nfds > up->u_rlimit[RLIMIT_NOFILE].rlim_cur)
		return (EINVAL);

	/*
	 * Need to allocate memory for pollstate before anything because
	 * the mutex and cv are created in this space
	 */
	if ((ps = curthread->t_pollstate) == NULL) {
		/*
		 * This is the first time this thread has ever polled,
		 * so we have to create its pollstate structure.
		 * This will persist for the life of the thread.
		 */
		ps = kmem_alloc(sizeof (pollstate_t), KM_SLEEP);
		curthread->t_pollstate = ps;
		ps->ps_nfds = 0;
		ps->ps_flag = 0;
		ps->ps_pollfd = NULL;
		ps->ps_polldat = NULL;
		ps->ps_busy = 0;

		/*
		 * These are destroyed and space released in thread_exit()
		 */
		mutex_init(&ps->ps_lock, "ps_lock", MUTEX_DEFAULT, NULL);
		mutex_init(&ps->ps_no_exit, "ps_no_exit", MUTEX_DEFAULT, NULL);
		cv_init(&ps->ps_cv, "poll_cv", CV_DEFAULT, NULL);
		cv_init(&ps->ps_busy_cv, "ps_busy_cv", CV_DEFAULT, NULL);
	}

	/*
	 * Check to see if this guy just wants to use poll() as a small
	 * timeout. If yes then bypass all the other stuff and make him
	 * sleep
	 */
	while (nfds == 0) {
		/*
		 * No fd's to poll so set a timeout and make it sleep
		 * Lock the thread so that a wakeup does not come
		 * after the timeout is set but before it goes to sleep
		 */
		mutex_enter(&ps->ps_lock);
		ps->ps_flag = 0;

		if ((msec_timo = uap->timo) >= 0) {
			/*
			 * convert timeout from milliseconds into ticks, and
			 * correct for the elapsed time since start of poll().
			 * The calculation hz_timo =
			 * (msec_timo * HZ + 999) / 1000
			 * is broken up to avoid intermediate integer overflow
			 * if msec_timo is large.
			 */
			int hz_timo;
			if (msec_timo < 2000) {
				/*
				 * This is the common case, and it can be
				 * handled in a computationally cheaper way.
				 */
				hz_timo = (msec_timo * HZ + 999) / 1000 -
					(lbolt - start);
			} else {
				int q = msec_timo / 1000;
				hz_timo = ((msec_timo - 1000 * q) * HZ + 999)
					/ 1000 + q * HZ - (lbolt - start);
			}

			if (hz_timo <= 0) {
				/*
				 * If we reach here that means we have
				 * already expired the timeout values that
				 * the user had asked us to wait. So
				 * drop the lock and just return back to
				 * the user.
				 */
				mutex_exit(&ps->ps_lock);
				return (0);
			}

			/*
			 * Set the timeout and put the process to sleep.
			 */
			ps->ps_flag |= T_POLLTIME;
			id = realtime_timeout((void(*)())polltime,
				(caddr_t)curthread, hz_timo);
		}

		if (!cv_wait_sig_swap(&ps->ps_cv, &ps->ps_lock)) {
			mutex_exit(&ps->ps_lock);
			if (msec_timo >= 0)
				(void) untimeout(id);
			return (EINTR);
		}

		/*
		 * If we were waked up by the expiration of the timer
		 * then return normally.  Otherwise we got a spurious
		 * wakeup; loop around and go to sleep again.
		 */
		if (msec_timo >= 0 && (ps->ps_flag & T_POLLTIME) == 0) {
			mutex_exit(&ps->ps_lock);
			return (0);
		}

		mutex_exit(&ps->ps_lock);
		if (msec_timo >= 0)
			(void) untimeout(id);
	}

	/*
	 * NOTE: for performance, buffers are saved across poll() calls.
	 * The theory is that if a process polls heavily, it tends to poll
	 * on the same set of descriptors.  Therefore, we only reallocate
	 * buffers when nfds changes.  There is no hysteresis control,
	 * because there is no data to suggest that this is necessary;
	 * the penalty of reallocating is not *that* great in any event.
	 */
	if (nfds != ps->ps_nfds) {

		/*
		 * Free old buffers
		 */
		int oldnfds = ps->ps_nfds;
		if ((pollfdp = ps->ps_pollfd) != NULL) {
			kmem_free(pollfdp, oldnfds * sizeof (pollfd_t));
			ps->ps_pollfd = NULL;
		}
		if ((pdp = ps->ps_polldat) != NULL) {
			kmem_free(pdp, oldnfds * sizeof (polldat_t));
			ps->ps_polldat = NULL;
		}
		ps->ps_nfds = 0;

		/*
		 * Allocate and initialize new buffers
		 */
		if (nfds != 0) {
			pollfdp =
			    kmem_alloc(nfds * sizeof (pollfd_t), KM_SLEEP);
			pdp =
			    kmem_alloc(nfds * sizeof (polldat_t), KM_SLEEP);
			ps->ps_pollfd = pollfdp;
			ps->ps_polldat = pdp;
			ps->ps_nfds = nfds;
			for (i = 0; i < nfds; i++) {
				pdp[i].pd_headp = NULL;
				pdp[i].pd_thread = curthread;
			}
		}
	}

	pollfdp = ps->ps_pollfd;
	if (nfds != 0) {
		if (copyin((caddr_t)uap->fdp, (caddr_t)pollfdp,
		    nfds * sizeof (pollfd_t)))
			return (EFAULT);
	}

	lwptotal = p->p_lwptotal;
	pdp = ps->ps_polldat;

	/*
	 * Separate into single and multi-threaded cases.
	 * Make a first pass without any locks held. We will also
	 * store the pollhead pointers for all fd's.
	 */

	/*
	 * Make quick pass without holding any lock to see if we found
	 * data on any fd. If ne then we will lock the thread to prevent
	 * a race condition with wakeup and scan once again. The second
	 * time we will need to also lock the pollhead structure before we
	 * do a VOP_POLL.
	 */
	if (lwptotal == 1) {
		register struct uf_entry *ufp = up->u_flist;
		int nofiles = up->u_nofiles;
		for (i = 0; i < nfds; i++) {
			register int fd = pollfdp[i].fd;
			int events = (ushort_t) pollfdp[i].events;
			pollhead_t *memphp = NULL;
			register file_t *fp;

			if (fd < 0) {
				pollfdp[i].revents = 0;
				continue;
			}
			if (fd >= nofiles) {
				pollfdp[i].revents = POLLNVAL;
				fdcnt++;
				continue;
			}
			if ((fp = ufp[fd].uf_ofile) == NULLFP) {
				pollfdp[i].revents = POLLNVAL;
				fdcnt++;
				continue;
			}
			error = VOP_POLL(fp->f_vnode, events,
				fdcnt, &pollfdp[i].revents, &memphp);

			if (error)
				goto pollout;

			if (pollfdp[i].revents)
				fdcnt++;
			/*
			 * Store the pollhead struct pointer here
			 */
			pdp[i].pd_sphp = memphp;
		}
	} else {
		/*
		 * multi threaded case. use multi threaded versions of
		 * getf/releasef.
		 */
		for (i = 0; i < nfds; i++) {
			int fd = pollfdp[i].fd;
			int events = (ushort_t) pollfdp[i].events;
			pollhead_t *memphp = NULL;
			file_t *fp;

			if (fd < 0) {
				pollfdp[i].revents = 0;
				continue;
			}
			if ((fp = getf(fd)) == NULL) {
				pollfdp[i].revents = POLLNVAL;
				fdcnt++;
				continue;
			}
			error = VOP_POLL(fp->f_vnode, events,
				fdcnt, &pollfdp[i].revents, &memphp);
			releasef(fd);

			if (error)
				goto pollout;

			if (pollfdp[i].revents)
				fdcnt++;
			/*
			 * Store the pollhead struct pointer here
			 */
			pdp[i].pd_sphp = memphp;
		}
	}

	/*
	 * Go back to the user if we found something without
	 * any locks at all. This is the perfect case because we
	 * did not have to acquire any locks.
	 */

	if (fdcnt) {
		goto pollout;
	}

	/*
	 * If you get here, the poll of fd's was unsuccessful.
	 * First make sure your timeout hasn't been reached.
	 * If it has already expired then there is no need to
	 * scan the fd's with the locks held. Just return back
	 * to the user.
	 */
	if ((msec_timo = uap->timo) >= 0) {

		int hz_timo;
		if (msec_timo < 2000) {
			hz_timo = (msec_timo * HZ + 999) / 1000 -
				(lbolt - start);
		} else {
			int q = msec_timo / 1000;
			hz_timo = ((msec_timo - 1000 * q) * HZ + 999) / 1000 +
				q * HZ - (lbolt - start);
		}
		if (hz_timo <= 0)
			goto pollout;
	}

	/*
	 * The second loop starts here with the lock on the thread held.
	 * This is to make sure that a wakeup does not come for a fd
	 * that we have just polled and put on the list off the pollhead
	 * structure. pollwakeup() will also lock the thread before
	 * calling setrun().
	 */
	mutex_enter(&ps->ps_lock);

retry:
	ps->ps_flag = 0;
	pdp = ps->ps_polldat;
	/*
	 * Separate into single and multi-threaded cases.
	 */
	if (lwptotal == 1) {
		register struct uf_entry *ufp = up->u_flist;
		int nofiles = up->u_nofiles;
		for (i = 0; i < nfds; i++) {
			register int fd = pollfdp[i].fd;
			int events = (ushort_t) pollfdp[i].events;
			pollhead_t *tphp, *php, *memphp = NULL;
			register file_t *fp;

			if (fd < 0) {
				pollfdp[i].revents = 0;
				continue;
			}
			if (fd >= nofiles) {
				pollfdp[i].revents = POLLNVAL;
				fdcnt++;
				continue;
			}
			if ((fp = ufp[fd].uf_ofile) == NULLFP) {
				pollfdp[i].revents = POLLNVAL;
				fdcnt++;
				continue;
			}

			/*
			 * Use the pollhead pointer that we saved in the
			 * first scan of the fd's to lock the
			 * pollhead structure.  This will make sure that
			 * a pollwakeup() is not running in parallel
			 * and removing threads from its list while we
			 * decide to put this thread on the same list.
			 */

			if (fdcnt == 0) {

				/*
				 * The case of fdcnt != or == 0 is
				 * different because we don't need to do
				 * any locking before VOP_POLL if fdcnt != 0
				 * We will never put this thread on the
				 * php returned by the VOP_POLL call in
				 * such a situation. So the need to lock
				 * he php before VOP_POLL does not exist
				 * and hence separate fdcnt cases
				 */

				tphp = ps->ps_polldat[i].pd_sphp;
				pplock(tphp);

				error = VOP_POLL(fp->f_vnode, events,
					fdcnt, &pollfdp[i].revents, &memphp);

				php = memphp;


				if (error) {
					ppunlock(tphp);
					polldel(ps);
					mutex_exit(&ps->ps_lock);
					goto pollout;
				}

				/*
				 * layered devices (e.g. console driver)
				 * may change the vnode and thus the pollhead
				 * pointer out from underneath us.
				 */
				if (php != NULL && php != tphp) {
					ppunlock(tphp);
					ps->ps_polldat[i].pd_sphp = php;
					goto retry;
				}

				if (pollfdp[i].revents)
					fdcnt++;
				else if (php != NULL) {
					/*
					 * No need to check fdcnt since it
					 * is zero at this point
					 */
					polldat_t *plist = php->ph_list;
					if (plist)
						plist->pd_prev = pdp;
					php->ph_list = pdp;
					pdp->pd_events = events;
					pdp->pd_next = plist;
					pdp->pd_prev = NULL;
					pdp->pd_headp = php;
					pdp++;
				}
				ppunlock(tphp);
			} else {

				error = VOP_POLL(fp->f_vnode, events,
					fdcnt, &pollfdp[i].revents, &memphp);
				/*
				 * The value in memphp is don't care since
				 * we do not plan to put this thread of that
				 * php anyway.
				 */
				if (error) {
					polldel(ps);
					mutex_exit(&ps->ps_lock);
					goto pollout;
				}
				if (pollfdp[i].revents)
					fdcnt++;
			}
		}
	} else {
		/*
		 * multi threaded case. use multi threaded versions of
		 * getf/releasef.
		 */
		for (i = 0; i < nfds; i++) {
			int fd = pollfdp[i].fd;
			int events = (ushort_t) pollfdp[i].events;
			pollhead_t *tphp, *php, *memphp = NULL;
			file_t *fp;

			if (fd < 0) {
				pollfdp[i].revents = 0;
				continue;
			}
			if ((fp = getf(fd)) == NULL) {
				pollfdp[i].revents = POLLNVAL;
				fdcnt++;
				continue;
			}

			if (fdcnt == 0) {

				tphp = ps->ps_polldat[i].pd_sphp;
				pplock(tphp);

				error = VOP_POLL(fp->f_vnode, events,
					fdcnt, &pollfdp[i].revents, &memphp);
				releasef(fd);

				php = memphp;

				if (error) {
					ppunlock(tphp);
					polldel(ps);
					mutex_exit(&ps->ps_lock);
					goto pollout;
				}

				/*
				 * layered devices (e.g. console driver)
				 * may change the vnode and thus the pollhead
				 * pointer out from underneath us.
				 */
				if (php != NULL && php != tphp) {
					ppunlock(tphp);
					ps->ps_polldat[i].pd_sphp = php;
					goto retry;
				}

				if (pollfdp[i].revents)
					fdcnt++;
				else if (php != NULL) {
					polldat_t *plist = php->ph_list;
					if (plist)
						plist->pd_prev = pdp;
					php->ph_list = pdp;
					pdp->pd_events = events;
					pdp->pd_next = plist;
					pdp->pd_prev = NULL;
					pdp->pd_headp = php;
					pdp++;
				}
				ppunlock(tphp);
			} else {

				error = VOP_POLL(fp->f_vnode, events,
					fdcnt, &pollfdp[i].revents, &memphp);

				releasef(fd);

				if (error) {
					polldel(ps);
					mutex_exit(&ps->ps_lock);
					goto pollout;
				}

				if (pollfdp[i].revents)
					fdcnt++;
			}
		}
	}

	if (fdcnt) {
		polldel(ps);
		mutex_exit(&ps->ps_lock);
		goto pollout;
	}

	/*
	 * If T_POLLWAKE is set, a pollwakeup() was performed on
	 * one of the file descriptors.  This can happen only if
	 * one of the VOP_POLL() functions dropped ps->ps_lock.
	 * The only current cases of this is in procfs (prpoll())
	 * and STREAMS (strpoll()).
	 */
	if (ps->ps_flag & T_POLLWAKE) {
		polldel(ps);
		goto retry;
	}

	/*
	 * If you get here, the poll of fds was unsuccessful.
	 * First make sure your timeout hasn't been reached.
	 * If not then sleep and wait until some fd becomes
	 * readable, writeable, or gets an exception.
	 */
	if ((msec_timo = uap->timo) >= 0) {

		int hz_timo;
		if (msec_timo < 2000) {
			hz_timo = (msec_timo * HZ + 999) / 1000 -
				(lbolt - start);
		} else {
			int q = msec_timo / 1000;
			hz_timo = ((msec_timo - 1000 * q) * HZ + 999) / 1000 +
				q * HZ - (lbolt - start);
		}
		if (hz_timo <= 0) {
			polldel(ps);
			mutex_exit(&ps->ps_lock);
			goto pollout;
		}
		ps->ps_flag |= T_POLLTIME;
		id = realtime_timeout((void(*)())polltime, (caddr_t)curthread,
			hz_timo);
	}

	/*
	 * The sleep will usually be awakened either by this poll's timeout
	 * (which will have cleared T_POLLTIME), or by the pollwakeup function
	 * called from either the VFS, the driver, or the stream head.
	 */
	if (!cv_wait_sig_swap(&ps->ps_cv, &ps->ps_lock)) {
		polldel(ps);
		mutex_exit(&ps->ps_lock);
		if (msec_timo >= 0)
			(void) untimeout(id);
		return (EINTR);
	}

	/*
	 * If T_POLLTIME is still set, you were awakened because an event
	 * occurred (data arrived, can write now, or exceptional condition).
	 * If so go back up and poll fds again. Otherwise, you've timed
	 * out so you will fall through and return.
	 */
	if (msec_timo < 0) {
		polldel(ps);
		goto retry;
	}
	if (ps->ps_flag & T_POLLTIME) {
		mutex_exit(&ps->ps_lock);
		(void) untimeout(id);
		mutex_enter(&ps->ps_lock);
		polldel(ps);
		goto retry;
	}

	polldel(ps);
	mutex_exit(&ps->ps_lock);

pollout:

	ASSERT(ps->ps_polldat->pd_headp == NULL);
	/*
	 * Poll cleanup code.
	 */
	if (error == 0) {
		/*
		 * Copy out the events and return the fdcnt to the user.
		 */
		rvp->r_val1 = fdcnt;
		if (nfds != 0)
			if (copyout((caddr_t)pollfdp, (caddr_t)uap->fdp,
			    nfds * sizeof (pollfd_t)))
				error = EFAULT;
	}
	return (error);
}


/*
 * This function is placed in the callout table to time out a process
 * waiting on poll.
 */
void
polltime(kthread_t *t)
{
	mutex_enter(&t->t_pollstate->ps_lock);
	if (t->t_wchan == (caddr_t)&t->t_pollstate->ps_cv && t->t_wchan0 == 0) {
		setrun(t);
		t->t_pollstate->ps_flag &= ~T_POLLTIME;
	}
	mutex_exit(&t->t_pollstate->ps_lock);
}

/*
 * This function is called to inform a thread that
 * an event being polled for has occurred.
 * The lock on the thread should be held on entry.
 */
void
pollrun(kthread_t *t)
{
	t->t_pollstate->ps_flag |= T_POLLWAKE;
	if (t->t_wchan == (caddr_t)&t->t_pollstate->ps_cv && t->t_wchan0 == 0)
		setrun(t);
	/*
	 * Now the polldel() is done by thread itself rather than here.
	 */
}

/*
 * This function deletes the polldat structures that were
 * added to various pollhead chains when polling began.
 * The lock on the thread should be held on entry.
 */
void
polldel(pollstate_t *ps)
{
	polldat_t *pdp, *pnext, *pprev;
	pollhead_t *php;
	int i;

	pdp = ps->ps_polldat;
	for (i = ps->ps_nfds; i > 0; i--) {

		/*
		 * Lock pollhead stucture before we modify its list
		 */

		pplock(pdp->pd_headp);

		if ((php = pdp->pd_headp) == NULL) {
			ppunlock(php);
			return;
		}

		pnext = pdp->pd_next;
		pprev = pdp->pd_prev;
		if (pnext)
			pnext->pd_prev = pprev;
		if (pprev)
			pprev->pd_next = pnext;
		else {
			php->ph_list = pnext;
			if (php->ph_list == NULL)
				ppwakeupemptylist(php);
		}
		pdp->pd_headp = NULL;
		pdp++;

		ppunlock(php);
	}
}


#define	NPTLOCKS	64	/* Number of locks */
#define	LOCKMASK	0x3F	/* Mask to AND with to get the lock index */
#define	LOCKSHIFT	8	/* Right shift address LOCKSHIFT bits */
				/* This is just to make sure that all the */
				/* the locks in tha array are used */

struct pplock	{
	kmutex_t	pp_lock;
	short		pp_flag;
	kcondvar_t	pp_wait_cv;
};
static struct pplock plocks[NPTLOCKS];	/* Hash array of pollhead locks */

/*
 * Called from mutex_init() to initalize the array of lockes
 */
void
pplock_init()
{
	int	ix;
	char	buf[128];

	for (ix = 0; ix < NPTLOCKS; ix++) {
		sprintf(buf, "pollh_lock %d", ix);
		mutex_init(&plocks[ix].pp_lock, buf, MUTEX_DEFAULT, NULL);
		cv_init(&plocks[ix].pp_wait_cv, "pollhead empty wait",
			CV_DEFAULT, NULL);
	}
}

/*
 * Hash function to use
 */
#define		HASH(X)	((((uint_t)(X)) >> LOCKSHIFT) & LOCKMASK)

/*
 * poll pollhead lock:
 *
 * Acquire a lock based on the pollhead structures address passed
 */
void
pplock(pollhead_t *php)
{
	kmutex_t *mutexp = &plocks[HASH(php)].pp_lock;
	mutex_enter(mutexp);
}

/*
 * poll pollhead unlock:
 *
 * Unlock the pollhead lock
 */
void
ppunlock(pollhead_t *php)
{
	kmutex_t *mutexp = &plocks[HASH(php)].pp_lock;
	mutex_exit(mutexp);
}

static void
ppwakeupemptylist(pollhead_t *php)
{
	struct pplock *pl = &plocks[HASH(php)];
	ASSERT(MUTEX_HELD(&pl->pp_lock));
	if (pl->pp_flag != 0) {
		pl->pp_flag = 0;
		cv_broadcast(&pl->pp_wait_cv);
	}
}

void
ppwaitemptylist(pollhead_t *php)
{
	struct pplock *pl = &plocks[HASH(php)];

	mutex_enter(&pl->pp_lock);
	while (php->ph_list != NULL) {
		pl->pp_flag = 1;
		cv_wait(&pl->pp_wait_cv, &pl->pp_lock);
	}
	mutex_exit(&pl->pp_lock);
}

/*
 * poll pollhead owned:
 *
 * Return true iff the pollhead lock is owned by the current thread.
 */
int
pplockowned(pollhead_t *php)
{
	kmutex_t *mutexp = &plocks[HASH(php)].pp_lock;
	return (mutex_owned(mutexp));
}
