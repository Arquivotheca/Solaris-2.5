/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)door_sys.c	1.14	95/07/20 SMI"

/*
 * System call I/F to doors (outside of vnodes I/F) and misc support
 * routines
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/door.h>
#include <sys/door_data.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/class.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/stack.h>
#include <sys/debug.h>
#include <sys/cpuvar.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/sobject.h>

#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <sys/vmsystm.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>
#include <vm/seg_vn.h>
#include <fs/proc/prdata.h>	/* Argh. Need prmapin/prmapout declarations */

#include <sys/modctl.h>
#include <sys/syscall.h>


static longlong_t doorfs(int, int, int, int, int, int);

static struct sysent door_sysent = {
	6,
	SE_ARGC | SE_NOUNLOAD,
	(int (*)())doorfs,
};

static struct modlsys modlsys = {
	&mod_syscallops, "doors", &door_sysent
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlsys, NULL
};

kcondvar_t door_cv;
dev_t	doordev;

extern	struct vfs door_vfs;
extern	struct vfsops door_vfsops;

_init()
{
	int	major;

	mutex_init(&door_knob, "door lock", MUTEX_DEFAULT, NULL);
	cv_init(&door_cv, "door server cv", CV_DEFAULT, NULL);
	if ((major = getudev()) == -1)
		return (ENXIO);
	doordev = makedevice(major, 0);

	/* Create a dummy vfs */
	door_vfs.vfs_op = &door_vfsops;
	door_vfs.vfs_flag = VFS_RDONLY;
	door_vfs.vfs_dev = doordev;
	door_vfs.vfs_fsid.val[0] = (long)doordev;
	door_vfs.vfs_fsid.val[1] = 0;

	return (mod_install(&modlinkage));
}

_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * System call wrapper for all door related system calls
 */
static longlong_t
doorfs(int arg1, int arg2, int arg3, int arg4, int arg5, int subcode)
{
	switch (subcode) {
	case DOOR_CALL:
		return (door_call(arg1, (caddr_t)arg2, arg3, arg4, arg5));
	case DOOR_RETURN:
		return (door_return((caddr_t)arg1, arg2, arg3, arg4,
							(caddr_t)arg5));
	case DOOR_CREATE:
		return (door_create((void (*)())arg1, (void *)arg2, arg3));
	case DOOR_REVOKE:
		return (door_revoke(arg1));
	case DOOR_INFO:
		return (door_info(arg1, (struct door_info *)arg2));
	case DOOR_CRED:
		return (door_cred((struct door_cred *)arg1));
	default:
		return (set_errno(EINVAL));
	}
}

void shuttle_resume(kthread_t *, kmutex_t *);
void shuttle_swtch(kmutex_t *);

/*
 * Support routines
 */
static int 	door_overflow(kthread_t *, caddr_t, u_int, u_int, u_int);
static void	door_server_exit(proc_t *, kthread_t *);
static void 	door_release_server(void);
static void	door_unref(void);
static void	door_wakeup(door_data_t	*);
static kthread_t	*door_get_server(door_node_t *);
static door_node_t	*door_lookup(int);

/*
 * System call to create a door
 */
int
door_create(void (*pc_cookie)(), void *data_cookie, u_int attributes)
{
	door_node_t	*dp;
	vnode_t		*vp;
	int		fd;
	struct file	*fp;
	static long	index = 0;
	extern	struct vnodeops door_vnodeops;

	if (attributes & ~(DOOR_UNREF))
		return (set_errno(EINVAL));

	dp = (door_node_t *)kmem_zalloc(sizeof (door_node_t), KM_SLEEP);

	mutex_enter(&door_knob);
	dp->door_target = curproc;
	dp->door_data = data_cookie;
	dp->door_pc = pc_cookie;
	dp->door_flags = attributes;
	vp = DTOV(dp);
	mutex_init(&vp->v_lock, "doors v_lock", MUTEX_DEFAULT, DEFAULT_WT);
	cv_init(&vp->v_cv, "vnode cv", CV_DEFAULT, NULL);
	vp->v_op = &door_vnodeops;
	vp->v_type = VDOOR;
	vp->v_vfsp = &door_vfs;
	vp->v_data = (caddr_t)vp;
	VN_HOLD(vp);
	dp->door_index = index++;

	if (falloc(vp, FREAD | FWRITE, &fp, NULL)) {
		mutex_exit(&door_knob);
		kmem_free(dp, sizeof (door_node_t));
		return (set_errno(EMFILE));
	}
	if ((fd = door_insert(fp)) == -1) {
		mutex_exit(&door_knob);
		unfalloc(fp);
		kmem_free(dp, sizeof (door_node_t));
		return (set_errno(EMFILE));
	}
	mutex_exit(&fp->f_tlock);
	mutex_exit(&door_knob);

	/* Set the close on exec flag for this descriptor */
	setpof(fd, FCLOSEXEC);
	return (fd);
}

/*
 * Invoked as:
 *
 *	error = door_call(int did, caddr_t bufp, int bufsize,
 *		   int arg_size, int ndid);
 */
longlong_t
door_call(int did, caddr_t bufp, u_int bufsize, u_int arg_size, u_int ndid)
{
	/* Locals */
	door_node_t	*dp = NULL;
	kthread_t	*server_thread;
	int		error = 0;
	struct file 	**fpp;
	door_desc_t	*didpp;
	door_desc_t	*close_fds = 0;	/* Saved copy of door_desc_t's */
	int		n_closefds;	/* Saved number of close fd's */
	klwp_id_t	lwp;
	door_data_t	*ct;		/* curthread door_data */
	door_data_t	*st;		/* server thread door_data */
	rval_t		rval;

	lwp = ttolwp(curthread);
	if ((ct = curthread->t_door) == NULL) {
		ct = curthread->t_door = kmem_zalloc(sizeof (door_data_t),
							KM_SLEEP);
	}
	/*
	 * Create an argument/result passing area if there is data to pass
	 */
	if ((ct->d_bsize = bufsize) != 0) {
		int	fpp_size;
		u_int	dsize = ndid * sizeof (door_desc_t);

		if (arg_size + dsize > bufsize)
			return (set_errno(EINVAL));
		if (bufsize > DOOR_MAX_BUF)
			return (set_errno(E2BIG));

		ct->d_buf = kmem_alloc(bufsize, KM_SLEEP);
		ct->d_asize = arg_size;
		ct->d_ndid = ndid;
		ct->d_fpp_size = fpp_size = ndid * sizeof (struct file *);
		ct->d_fpp = fpp_size ? kmem_alloc(fpp_size, KM_SLEEP) : 0;
		/*
		 * Copyin the argument data into a temporary buffer.
		 */
		if (arg_size && copyin(bufp, ct->d_buf, arg_size)) {
			error = EFAULT;
			goto out;
		}
		/*
		 * Copyin the door args and translate them into files
		 */
		if (ndid) {
			int	door_moves = 0;

			n_closefds = ndid;
			didpp = (door_desc_t *)&ct->d_buf[bufsize - dsize];
			/* door_desc_t must be aligned */
			if ((u_int)didpp & (sizeof (int) - 1) ||
			    copyin(&bufp[bufsize - dsize], (caddr_t)didpp,
					dsize)) {
				error = EFAULT;
				goto out;
			}
			fpp = ct->d_fpp;
			while (ndid--) {
				struct file *fp;

				if ((fp = GETF(didpp->d_descriptor)) == NULL) {
					door_fp_close(ct->d_fpp, fpp-ct->d_fpp);
					error = EINVAL;
					goto out;
				}
				/* Hold the fp */
				mutex_enter(&fp->f_tlock);
				fp->f_count++;
				mutex_exit(&fp->f_tlock);

				if (didpp->d_attributes & DOOR_MOVE)
					door_moves = 1;

				*fpp = fp;
				RELEASEF(didpp->d_descriptor);
				fpp++; didpp++;
			}
			/*
			 * If we have to close some descriptors upon return,
			 * copy them to a safe place.
			 */
			if (door_moves) {
				close_fds = kmem_alloc(dsize, KM_SLEEP);
				bcopy(&ct->d_buf[bufsize - dsize],
					(caddr_t)close_fds, dsize);
			}
		}
	} else {
		ct->d_asize = 0;
		ct->d_ndid = 0;
	}
	mutex_enter(&door_knob);
	if ((dp = door_lookup(did)) == NULL || DOOR_INVALID(dp)) {
		mutex_exit(&door_knob);
		door_fp_close(ct->d_fpp, ct->d_ndid);
		error = EBADF;
		goto out;
	}
	/*
	 * Get a server thread from the target domain
	 */
	if ((server_thread = door_get_server(dp)) == NULL) {
		mutex_exit(&door_knob);
		door_fp_close(ct->d_fpp, ct->d_ndid);
		error = EAGAIN;
		goto out;
	}

	dp->door_active++;

	ct->d_overflow = 0;
	ct->d_error = DOOR_WAIT;
	st = server_thread->t_door;
	st->d_caller = curthread;
	st->d_active = dp;

	/* Feats don`t fail me now... */
	shuttle_resume(server_thread, &door_knob);

	mutex_enter(&door_knob);
shuttle_return:
	if ((error = ct->d_error) < 0) {
		/*
		 * Premature wakeup. Find out why (stop, fork, signal, exit ...)
		 */
		mutex_exit(&door_knob);		/* May block in ISSIG */
		if (ISSIG(curthread, FORREAL) ||
		    lwp->lwp_sysabort || ISHOLD(curproc)) {
			/* Signal, fork, ... */
			lwp->lwp_sysabort = 0;
			mutex_enter(&door_knob);
			error = EINTR;
			/*
			 * If the server hasn't exited,
			 * let it know we are not interested anymore.
			 */
			if (ct->d_error != DOOR_EXIT &&
			    st->d_caller == curthread) {
				st->d_active = NULL;
				st->d_caller = NULL;
				/* Send cancellation here */
			}
		} else {
			/*
			 * Return from stop(), server exit...
			 *
			 * Note that the server could have done a
			 * door_return while the client was in stop state
			 * (ISSIG), in which case the error condition
			 * is updated by the server.
			 */
			mutex_enter(&door_knob);
			if (ct->d_error == DOOR_WAIT) {
				/* Still waiting for a reply */
				shuttle_swtch(&door_knob);
				mutex_enter(&door_knob);
				lwp->lwp_asleep = 0;
				goto	shuttle_return;
			} else if (ct->d_error == DOOR_EXIT) {
				/* Server exit */
				error = EINTR;
			} else {
				/* Server did a door_return during ISSIG */
				error = ct->d_error;
			}
		}
		/*
		 * Can't exit if the client is currently copying
		 * results for me
		 */
		while (ct->d_flag & DOOR_HOLD) {
			ct->d_flag |= DOOR_WAITING;
			cv_wait(&ct->d_cv, &door_knob);
		}
	}
	lwp->lwp_asleep = 0;		/* /proc */
	lwp->lwp_sysabort = 0;		/* /proc */
	if (--dp->door_active == 0 && (dp->door_flags & DOOR_DELAY_UNREF))
		door_deliver_unref(dp);
	mutex_exit(&door_knob);

	/*
	 * Move the result args to userland (if any)
	 */
	if (error)
		goto out;

	/*
	 * Return the actual amount of data/descriptors returned
	 */
	arg_size = ct->d_asize;
	dr_set_actual_size(lwp->lwp_regs, arg_size);
	ndid = ct->d_ndid;
	dr_set_actual_ndid(lwp->lwp_regs, ndid);
	if (ct->d_overflow) {
		/* The data has already been copied there */
		bufp = ct->d_overflow;
		dr_set_buf_size(lwp->lwp_regs, ct->d_olen);
	} else {
		if (arg_size && copyout(ct->d_buf, bufp, arg_size)) {
			door_fp_close(ct->d_fpp, ct->d_ndid);
			error = EFAULT;
			goto out;
		}
		dr_set_buf_size(lwp->lwp_regs, bufsize);
	}

	/*
	 * stuff returned doors into our proc, copyout the descriptors
	 */
	if (ndid) {
		door_desc_t *start;
		int dsize = ndid * sizeof (door_desc_t);

		if (ct->d_overflow)
			bufsize = ct->d_olen;
		else
			bufsize = ct->d_bsize;

		/* Re-use d_buf to hold door_desc_t's */
		start = didpp = (door_desc_t *)&ct->d_buf[0],
		fpp = ct->d_fpp;

		mutex_enter(&door_knob);
		while (ndid--) {
			didpp->d_descriptor = door_insert(*fpp);
			didpp->d_attributes = door_attributes(*fpp);
			if (didpp->d_descriptor == -1) {
				/* Cleanup newly created fd's */
				mutex_exit(&door_knob);
				door_fd_close(start, didpp - start, 0);
				/* Close remaining files */
				door_fp_close(fpp, ndid + 1);
				error = EMFILE;
				goto out;
			}
			fpp++; didpp++;
		}
		mutex_exit(&door_knob);

		if (copyout((caddr_t)start, &bufp[bufsize - dsize], dsize)) {
			door_fd_close(start, ct->d_ndid, 0);
			error = EFAULT;
			goto out;
		}
	}
out:
	if (dp)
		RELEASEF(did);

	if (ct->d_bsize) {
		kmem_free(ct->d_buf, ct->d_bsize);
		if (ct->d_fpp_size) {
			kmem_free(ct->d_fpp, ct->d_fpp_size);
			ct->d_fpp_size = 0;
		}
		/* Close all the fd's marked with DOOR_MOVE */
		if (close_fds != NULL) {
			if (error == 0) {
				door_fd_close(close_fds, n_closefds, DOOR_MOVE);
			}
			kmem_free(close_fds, n_closefds * sizeof (door_desc_t));
		}
	}
	if (error) {
		return (set_errno(error));
	} else {
		rval.r_val1 = 0;
		rval.r_val2 = (int)bufp;
		return (rval.r_vals);
	}
}

/*
 * Invoked as:
 *
 *	error = door_return(caddr_t resultp, int result_size,
 *			int actual_size, int actual ndid, caddr_t stk_base)
 */
longlong_t
door_return(caddr_t bufp, u_int bufsize, u_int adata, u_int a_did, caddr_t sp)
{
	/* Locals */
	struct file	**fpp;
	door_desc_t	*didpp;
	kthread_t	*caller;
	klwp_t		*lwp;
	int		error = 0;
	int		i;
	door_node_t	*dp;
	door_data_t	*ct;		/* curthread door_data */
	door_data_t	*caller_t;	/* caller door_data */

	if ((ct = curthread->t_door) == NULL) {
		ct = curthread->t_door = kmem_zalloc(sizeof (door_data_t),
						KM_SLEEP);
	}
	/* Make sure the caller hasn't gone away */
	mutex_enter(&door_knob);
	if ((caller = ct->d_caller) == NULL)
		goto out;
	if ((caller_t = caller->t_door) == NULL)
		goto out;
	/*
	 * Copyin the result data to our (callers) temp buffer.
	 */
	if (bufsize != 0) {
		u_int	dsize;

		dsize = a_did * sizeof (door_desc_t);
		if (adata + dsize > bufsize) {
			mutex_exit(&door_knob);
			return (set_errno(EINVAL));
		}
		/*
		 * Prevent the client from exiting until we have finished
		 * moving results. Don't hold door_knob during copyin
		 */
		caller_t->d_flag = DOOR_HOLD;
		mutex_exit(&door_knob);

		if (bufsize > caller_t->d_bsize) {
			/* Overflow case */
			error = door_overflow(caller, bufp, bufsize,
					adata, a_did);
			door_wakeup(caller_t);
			if (error)
				return (set_errno(error));
			mutex_enter(&door_knob);
			goto out;
		}
		if (adata && copyin(bufp, caller_t->d_buf, adata)) {
			door_wakeup(caller_t);
			return (set_errno(EFAULT));
		}
		/*
		 * Copyin the returned door ids and
		 * translate them into door_node_t *s
		 */
		if (a_did) {
			door_desc_t *start;
			int	door_moves = 0;

			start = didpp = (door_desc_t *)
				&caller_t->d_buf[caller_t->d_bsize - dsize];
			if ((u_int)didpp & (sizeof (int) - 1)) {
				/* Caller gave us a mis-aligned buffer */
				door_wakeup(caller_t);
				return (set_errno(EFAULT));
			}
			if (copyin(&bufp[bufsize-dsize], (caddr_t)didpp,
								dsize)) {
				door_wakeup(caller_t);
				return (set_errno(EFAULT));
			}
			if (a_did > caller_t->d_ndid) {
				/* make more space */
				kmem_free(caller_t->d_fpp,
					caller_t->d_fpp_size);
				caller_t->d_fpp_size =
					a_did * sizeof (door_node_t *);
				caller_t->d_fpp = kmem_alloc(
					caller_t->d_fpp_size, KM_SLEEP);
			}
			fpp = caller_t->d_fpp;

			for (i = 0; i < a_did; i++) {
				struct file *fp;

				if ((fp = GETF(didpp->d_descriptor)) == NULL) {
					door_fp_close(caller_t->d_fpp,
							fpp - caller_t->d_fpp);
					door_wakeup(caller_t);
					return (set_errno(EINVAL));
				}

				mutex_enter(&fp->f_tlock);
				fp->f_count++;
				mutex_exit(&fp->f_tlock);

				if (didpp->d_attributes & DOOR_MOVE)
					door_moves = 1;

				*fpp = fp;
				RELEASEF(didpp->d_descriptor);
				fpp++; didpp++;
			}
			/* Close all DOOR_MOVE descriptors now */
			if (door_moves)
				door_fd_close(start, a_did, DOOR_MOVE);
		}
		mutex_enter(&door_knob);
		if (caller_t->d_flag & DOOR_WAITING) {
			cv_signal(&caller_t->d_cv);
		}
		caller_t->d_flag = 0;
	}
	caller_t->d_ndid = a_did;
	caller_t->d_asize = adata;
out:
	/* Put ourselves on the available server thread list */
	door_release_server();

	/*
	 * Make sure the caller is still waiting to be resumed
	 */
	if (caller) {
		disp_lock_t *tlp;

		thread_lock(caller);
		caller_t->d_error = error;	/* Return any errors */
		if (caller->t_state == TS_SLEEP &&
		    SOBJ_TYPE(caller->t_sobj_ops) == SOBJ_SHUTTLE) {
			tlp = caller->t_lockp;
			/*
			 * Setting t_disp_queue prevents erroneous preemptions
			 * if this thread is still in execution on another
			 * processor
			 */
			caller->t_disp_queue = &CPU->cpu_disp;
			THREAD_ONPROC(caller, CPU);
			/*
			 * Make sure we end up on the right CPU if we
			 * are dealing with bound CPU's
			 */
			if (caller->t_bound_cpu != NULL) {
				aston(caller);
				CPU->cpu_runrun = 1;
			}
			disp_lock_exit_high(tlp);
			shuttle_resume(caller, &door_knob);
		} else {
			/* May have been setrun or in stop state */
			thread_unlock(caller);
			shuttle_swtch(&door_knob);
		}
	} else {
		shuttle_swtch(&door_knob);
	}

	/*
	 * We've sprung to life. Determine if we are part of a door
	 * invocation, or just interrupted
	 */
	lwp = ttolwp(curthread);
	mutex_enter(&door_knob);
	if ((dp = ct->d_active) != NULL) {
		longlong_t	rval;

		/*
		 * Normal door invocation. Return any error condition
		 * encountered while trying to pass args to the server
		 * thread.
		 */
		if (DOOR_INVALID(dp)) {
			error = EBADF;
			caller = ct->d_caller;
			caller_t = caller->t_door;
			goto out;
		}
		lwp->lwp_asleep = 0;
		/*
		 * Prevent the caller from leaving us while we
		 * are copying out the arguments from it's buffer
		 */
		caller_t = ct->d_caller->t_door;
		caller_t->d_flag = DOOR_HOLD;
		mutex_exit(&door_knob);

		rval = door_server_dispatch(caller_t, dp, sp, &error);

		mutex_enter(&door_knob);
		/*
		 * If the caller was trying to exit while we dispatched
		 * this thread, signal it now.
		 */
		if (caller_t->d_flag & DOOR_WAITING)
			cv_signal(&caller_t->d_cv);
		caller_t->d_flag = 0;
		if (error) {
			caller = ct->d_caller;
			caller_t = caller->t_door;
			goto out;
		}
		mutex_exit(&door_knob);
		return (rval);
	} else {
		/*
		 * We are not involved in a door_invocation.
		 * Check for /proc related activity...
		 */
		ct->d_caller = NULL;
		door_server_exit(curproc, curthread);
		mutex_exit(&door_knob);
		if (ISSIG(curthread, FORREAL) ||
		    lwp->lwp_sysabort || ISHOLD(curproc)) {
			lwp->lwp_asleep = 0;
			lwp->lwp_sysabort = 0;
			return (set_errno(EINTR));
		} else {
			/* Go back and wait for another request */
			lwp->lwp_asleep = 0;
			mutex_enter(&door_knob);
			caller = NULL;
			goto out;
		}
	}
}

/*
 * Revoke any future invocations on this door
 */
int
door_revoke(int did)
{
	door_node_t	*d;
	struct file 	*fp;

	mutex_enter(&door_knob);
	if ((d = door_lookup(did)) == NULL) {
		mutex_exit(&door_knob);
		return (set_errno(EBADF));
	}
	if (d->door_target != curproc) {
		mutex_exit(&door_knob);
		RELEASEF(did);
		return (set_errno(EPERM));
	}
	d->door_flags |= DOOR_REVOKED;
	cv_broadcast(&door_cv);
	mutex_exit(&door_knob);
	RELEASEF(did);
	/* Invalidate the descriptor */
	if ((fp = getandset(did)) == NULL)
		return (set_errno(EBADF));
	return (closef(fp));
}

int
door_info(int did, struct door_info *d_info)
{
	door_node_t	*dp;
	door_info_t	di;
	door_info_t	*dip;

	dip = &di;
	bzero((caddr_t)dip, sizeof (door_info_t));

	mutex_enter(&door_knob);
	if ((dp = door_lookup(did)) == NULL) {
		/* Not a door */
		mutex_exit(&door_knob);
		return (set_errno(EBADF));
	} else {
		if (DOOR_INVALID(dp)) {
			dip->di_attributes |= DOOR_REVOKED;
			dip->di_target = -1;
		} else {
			dip->di_target = dp->door_target->p_pidp->pid_id;
		}
		if (dp->door_target == curproc)
			dip->di_attributes |= DOOR_LOCAL;
		if (dp->door_flags & DOOR_UNREF)
			dip->di_attributes |= DOOR_UNREF;
		dip->di_proc = (door_ptr_t)dp->door_pc;
		dip->di_data = (door_ptr_t)dp->door_data;
		dip->di_uniqifier = dp->door_index;
		RELEASEF(did);
	}
	mutex_exit(&door_knob);

	if (copyout((caddr_t)&di, (caddr_t)d_info, sizeof (struct door_info))) {
		return (set_errno(EFAULT));
	}
	return (0);
}

/*
 * Return credentials of the door caller (if any) for this invocation
 */
int
door_cred(struct door_cred *d_cred)
{
	door_cred_t	dc;
	kthread_t	*caller;
	door_data_t	*ct;
	struct cred	*cred;
	struct proc	*p;

	mutex_enter(&door_knob);
	if ((ct = curthread->t_door) == NULL ||
	    (caller = ct->d_caller) == NULL) {
		mutex_exit(&door_knob);
		return (set_errno(EINVAL));
	}

	/* Get the credentials of the calling process */
	p = ttoproc(caller);
	mutex_enter(&p->p_crlock);
	cred = p->p_cred;
	dc.dc_euid = cred->cr_uid;
	dc.dc_egid = cred->cr_gid;
	dc.dc_ruid = cred->cr_ruid;
	dc.dc_rgid = cred->cr_rgid;
	mutex_exit(&p->p_crlock);

	dc.dc_pid = p->p_pidp->pid_id;
	mutex_exit(&door_knob);

	if (copyout((caddr_t)&dc, (caddr_t)d_cred, sizeof (door_cred_t)))
		return (set_errno(EFAULT));
	return (0);
}

/*
 * Return attributes associated with this (possible) door
 */
int
door_attributes(struct file *fp)
{
	struct vnode *vp;
	int	attributes = 0;

	if (VOP_REALVP(fp->f_vnode, &vp))
		vp = fp->f_vnode;
	if (vp && vp->v_type == VDOOR) {
		if (VTOD(vp)->door_target == curproc)
			attributes |= DOOR_LOCAL;
		if (VTOD(vp)->door_flags & DOOR_UNREF)
			attributes |= DOOR_UNREF;
	}
	return (attributes);
}

/*
 * Create a descriptor for the associated file.
 *
 * If the file refers to a door, bump the reference count on the
 * door so we can deliver unrefs at the approriate time
 */
int
door_insert(struct file *fp)
{
	int	fd;

	if (ufalloc(0, &fd))
		return (-1);
	setf(fd, fp);
	return (fd);
}

/*
 * Return an available thread for this server.
 */
static kthread_t *
door_get_server(door_node_t *dp)
{
	kthread_t *server_t;
	proc_t	*pp;

	ASSERT(MUTEX_HELD(&door_knob));

	pp = dp->door_target;
	ASSERT(pp != &p0);
	while ((server_t = pp->p_server_threads) == NULL) {
		if (!cv_wait_sig_swap(&door_cv, &door_knob))
			return (NULL);	/* Got a signal */
		if (DOOR_INVALID(dp))
			return (NULL);	/* Target is invalid now */
	}
	ASSERT(server_t != NULL);

	thread_lock(server_t);
	if (server_t->t_state == TS_SLEEP &&
	    SOBJ_TYPE(server_t->t_sobj_ops) == SOBJ_SHUTTLE) {
		/*
		 * Mark the thread as ONPROC and take it off the list
		 * of available server threads. We are committed to
		 * resuming this thread now.
		 */
		disp_lock_t *tlp = server_t->t_lockp;

		pp->p_server_threads = server_t->t_door->d_servers;
		server_t->t_door->d_servers = NULL;
		/*
		 * Setting t_disp_queue prevents erroneous preemptions
		 * if this thread is still in execution on another processor
		 */
		server_t->t_disp_queue = &CPU->cpu_disp;
		THREAD_ONPROC(server_t, CPU);
		/*
		 * Make sure we end up on the right CPU if we
		 * are dealing with bound CPU's
		 */
		if (server_t->t_bound_cpu != NULL) {
			aston(server_t);
			CPU->cpu_runrun = 1;
		}
		disp_lock_exit(tlp);
		return (server_t);
	} else {
		/* Not kosher to resume this thread */
		thread_unlock(server_t);
		return (NULL);
	}
}

/*
 * Put a server thread back in the pool.
 */
static void
door_release_server()
{
	proc_t	*p = curproc;
	door_data_t *ct = curthread->t_door;

	ASSERT(MUTEX_HELD(&door_knob));
	ct->d_error = 0;
	ct->d_active = NULL;
	ct->d_caller = NULL;
	ct->d_servers = p->p_server_threads;
	p->p_server_threads = curthread;

	/* Wakeup any blocked door calls */
	cv_broadcast(&door_cv);
}

/*
 * Remove a server thread from the pool if present.
 */
static void
door_server_exit(proc_t *p, kthread_t *t)
{
	kthread_t **next;

	ASSERT(MUTEX_HELD(&door_knob));
	for (next = &p->p_server_threads; *next != NULL;
			next = &((*next)->t_door->d_servers)) {
		if (*next == t) {
			*next = t->t_door->d_servers;
			return;
		}
	}
}

/*
 * Lookup the door descriptor. Caller must call RELEASEF when finished
 * with associated door.
 */
static door_node_t *
door_lookup(int did)
{
	vnode_t	*vp;
	file_t *fp;

	ASSERT(MUTEX_HELD(&door_knob));

	if ((fp = GETF(did)) == NULL)
		return (NULL);
	/*
	 * Use the underlying vnode (we may be namefs mounted)
	 */
	if (VOP_REALVP(fp->f_vnode, &vp))
		vp = fp->f_vnode;

	if (vp == NULL || vp->v_type != VDOOR) {
		RELEASEF(did);
		return (NULL);
	}

	return (VTOD(vp));
}

/*
 * The current thread is exiting, so clean up any pending
 * invocation details
 */
void
door_slam(void)
{
	door_node_t *dp;
	door_data_t *ct;
	/*
	 * If we are an active door server, notify our
	 * client that we are exiting and revoke our door.
	 */
	if ((ct = curthread->t_door) == NULL)
		return;
	mutex_enter(&door_knob);
	curthread->t_door = NULL;
	if ((dp = ct->d_active) != NULL) {
		kthread_id_t t = ct->d_caller;

		ASSERT(ct->d_caller);
		/* Revoke our door if the process is exiting */
		if (dp->door_target == curproc &&
		    (curproc->p_flag & EXITLWPS)) {
			dp->door_target = NULL;
			dp->door_flags |= DOOR_REVOKED;
			cv_broadcast(&door_cv);
		}
		/*
		 * Let the caller know we are gone
		 */
		t->t_door->d_error = DOOR_EXIT;
		thread_lock(t);
		if (t->t_state == TS_SLEEP &&
			    SOBJ_TYPE(t->t_sobj_ops) == SOBJ_SHUTTLE)
			setrun_locked(t);
		thread_unlock(t);
	}
	mutex_exit(&door_knob);
	kmem_free(ct, sizeof (door_data_t));
}


/*
 * Deliver queued unrefs to appropriate door server.
 */
static void
door_unref()
{
	kthread_t	*server_thread;
	door_node_t	*dp;
	door_data_t	*ct;
	door_data_t	*st;

	mutex_enter(&door_knob);
	for (;;) {
		/* Grab a queued request */
		while ((dp = curproc->p_unref_list) == NULL) {
			if (!cv_wait_sig(&curproc->p_server_cv, &door_knob)) {
				curproc->p_unref_thread = 0;
				mutex_exit(&door_knob);
				mutex_enter(&curproc->p_lock);
				lwp_exit();
				/* NOTREACHED */
			}
		}
		curproc->p_unref_list = dp->door_ulist;
		dp->door_ulist = NULL;

		/* Quicky door invocation */
		if ((ct = curthread->t_door) == NULL) {
			ct = curthread->t_door = kmem_zalloc(
				sizeof (door_data_t), KM_SLEEP);
		}
		ct->d_bsize = 0;
		ct->d_fpp_size = 0;
		ct->d_asize = (int)DOOR_UNREF_DATA;
		ct->d_ndid = 0;
		dp->door_active++;
		/*
		 * Get a server thread from the target domain
		 */
		server_thread = door_get_server(dp);
		if (server_thread == NULL) {
			VN_RELE(DTOV(dp));
			continue;
		}

		st = server_thread->t_door;
		st->d_caller = curthread;
		st->d_active = dp;

		shuttle_resume(server_thread, &door_knob);
		mutex_enter(&door_knob);
		if (ct->d_error != DOOR_EXIT &&
		    st->d_caller == curthread) {
			st->d_active = NULL;
			st->d_caller = NULL;
		}

		dp->door_active--;
		VN_RELE(DTOV(dp));
	}
}

/*
 * Queue an unref invocation for processing for the current process
 */
void
door_deliver_unref(door_node_t *d)
{
	struct proc *server = d->door_target;

	ASSERT(MUTEX_HELD(&door_knob));
	ASSERT(d->door_active == 0);

	if (server == NULL)
		return;
	/*
	 * Create a lwp to deliver unref calls if one isn't already running.
	 */
	if (!server->p_unref_thread) {
		/*
		 * Uses the attributes of the first thread in the server.
		 */
		kthread_t	*first;

		if ((first = server->p_tlist) == NULL)
			return;
		if (lwp_create(door_unref, 0, 0, server, TS_RUN,
			    first->t_pri, first->t_hold, first->t_cid) != NULL)
			server->p_unref_thread = 1;
	}
	VN_HOLD(DTOV(d));
	/* Only 1 unref per door */
	d->door_flags &= ~(DOOR_UNREF|DOOR_DELAY_UNREF);
	d->door_ulist = server->p_unref_list;
	server->p_unref_list = d;
	cv_broadcast(&server->p_server_cv);
}

/*
 * The callers buffer isn't big enough for all of the data/fd's. Allocate
 * space in the callers address space for the results. And copy the data
 * there.
 */
static int
door_overflow(
	kthread_t	*caller,
	caddr_t 	bufp,		/* buffer location */
	u_int		bsize,		/* buffer size */
	u_int		alen,		/* data size */
	u_int		ndid)		/* number of fd's */
{
	struct as *as = ttoproc(caller)->p_as;
	door_data_t *ct = caller->t_door;
	caddr_t	addr;			/* Resulting address in target */
	int	rlen;			/* Rounded len */
	int	len;
	int	i;
	int	error;

	ASSERT(ct->d_flag & DOOR_HOLD);
	/*
	 * Allocate space for this stuff in the callers address space
	 */
	rlen = roundup(bsize, PAGESIZE);
	as_rangelock(as);
	map_addr_proc(&addr, rlen, 0, 1, ttoproc(caller));
	if (addr == NULL) {
		/* No virtual memory available */
		as_rangeunlock(as);
		return (E2BIG);
	}
	if ((error = as_map(as, addr, rlen, segvn_create, zfod_argsp)) != 0) {
		as_rangeunlock(as);
		return (error);
	}
	as_rangeunlock(as);

	if (alen) {
		caddr_t	src = bufp;
		caddr_t saddr = addr;

		/* Copy any data */
		for (i = 0, len = alen; i < alen; i += PAGESIZE) {
			caddr_t	vaddr;
			int	amount;

			if (as_fault(as->a_hat, as, saddr,
					PAGESIZE, F_SOFTLOCK, S_WRITE)) {
				as_unmap(as, addr, rlen);
				return (E2BIG);
			}
			/*
			 * XXX. I don't think there is any reason
			 * to grab the 'as' lock here since we prevent
			 * exits and have soft-locked the saddr pages.
			 */
			AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
			vaddr = prmapin(as, saddr, 1);
			AS_LOCK_EXIT(as, &as->a_lock);
			amount = len > PAGESIZE ? PAGESIZE : len;
			if (copyin(src, vaddr, amount)) {
				AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
				prmapout(as, src, vaddr, 1);
				AS_LOCK_EXIT(as, &as->a_lock);
				as_fault(as->a_hat, as, saddr, PAGESIZE,
					F_SOFTUNLOCK, S_WRITE);
				as_unmap(as, addr, rlen);
				return (EFAULT);
			}
			AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
			prmapout(as, src, vaddr, 1);
			AS_LOCK_EXIT(as, &as->a_lock);
			/* Unlock the data in target address space */
			as_fault(as->a_hat, as, saddr, PAGESIZE,
					F_SOFTUNLOCK, S_WRITE);
			saddr += PAGESIZE;
			src += PAGESIZE;
			len -= PAGESIZE;
		}
	}
	/* Copy any fd's */
	if (ndid) {
		int		dsize = ndid * sizeof (door_desc_t);
		door_desc_t	*didpp, *start;
		struct file	**fpp;
		int		door_moves = 0;
		struct user	*up = PTOU(ttoproc(caller));

		if (ndid > up->u_rlimit[RLIMIT_NOFILE].rlim_cur) {
			as_unmap(as, addr, rlen);
			return (EINVAL);
		}
		start = didpp = kmem_alloc(dsize, KM_SLEEP);
		if (copyin(&bufp[bsize - dsize], (caddr_t)didpp, dsize)) {
			kmem_free(didpp, dsize);
			as_unmap(as, addr, rlen);
			return (EFAULT);
		}
		if (ndid > ct->d_ndid) {
			/* make more space */
			if (ct->d_fpp_size)
				kmem_free(ct->d_fpp, ct->d_fpp_size);
			ct->d_fpp_size = ndid * sizeof (struct file *);
			ct->d_fpp = kmem_alloc(ct->d_fpp_size, KM_SLEEP);
		}
		fpp = ct->d_fpp;

		for (i = 0; i < ndid; i++) {
			struct file *fp;

			if ((fp = GETF(didpp->d_descriptor)) == NULL) {
				door_fp_close(ct->d_fpp, fpp - ct->d_fpp);
				kmem_free(start, dsize);
				as_unmap(as, addr, rlen);
				return (EINVAL);
			}
			mutex_enter(&fp->f_tlock);
			fp->f_count++;
			mutex_exit(&fp->f_tlock);

			if (didpp->d_attributes & DOOR_MOVE)
				door_moves = 1;
			*fpp = fp;
			RELEASEF(didpp->d_descriptor);
			fpp++; didpp++;
		}
		if (door_moves)
			door_fd_close(start, ndid, DOOR_MOVE);
		/*
		 * Resize the callers buffer so it can use this to create
		 * a work area to build fd's in it's address space
		 */
		if (ct->d_bsize)
			kmem_free(ct->d_buf, ct->d_bsize);
		ct->d_buf = (caddr_t)start;
		ct->d_bsize = dsize;
	}
	ct->d_asize = alen;
	ct->d_ndid = ndid;
	ct->d_overflow = addr;
	ct->d_olen = rlen;
	return (0);
}

/*
 * Close all the descriptors marked with attribute 'flag'
 * A flag of 0 means close the decriptor regardless of attributes
 */
void
door_fd_close(door_desc_t *d, int n, u_int flags)
{
	int	i;
	file_t	*fp;

	for (i = 0; i < n; i++) {
		if (flags == 0 || (d->d_attributes & flags)) {
			fp = getandset(d->d_descriptor);
			if (fp)
				closef(fp);
		}
		d++;
	}
}

/*
 * Decrement ref count on all the files passed
 */
void
door_fp_close(struct file **fp, int n)
{
	int	i;

	for (i = 0; i < n; i++)
		closef(fp[i]);
}

/*
 * Wakeup the caller
 */
static void
door_wakeup(door_data_t	*ct)
{
	mutex_enter(&door_knob);
	ct->d_flag = 0;
	cv_signal(&ct->d_cv);
	mutex_exit(&door_knob);
}
