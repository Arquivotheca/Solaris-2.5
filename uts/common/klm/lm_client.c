/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 *
 *    Copyright (c) 1994, 1995 by Sun Microsystems, Inc.
 *    All rights reserved.
 */

#pragma ident "@(#)lm_client.c	1.40	95/07/19 SMI"	/* NCR OS2.00.00 1.1 */

/*
 * This is the interface routines for the client side of the Lock
 * Manager. See the LM protocol specification for a description of
 * this interface.
 */
#include <sys/types.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/flock.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <nfs/lm.h>
#include <fs/fs_subr.h>

/*
 * map NLM return status value into an errno value
 */
int
lm_stat_to_errno[] = {
	0,	/* nlm_granted */
	EAGAIN,	/* nlm_denied */
	ENOLCK,	/* nlm_denied_nolocks */
	EAGAIN,	/* nlm_blocked */
	EAGAIN,	/* nlm_denied_grace_period */
	EDEADLK	/* nlm_deadlck */
};

/* Forward declarations of static functions. */
static void register_lock_locally(vnode_t *vp, struct lm_sysid *ls,
		struct flock *flk, int flag, off_t offset);

/*
 * lm_frlock
 *
 * This function calls the server and the local locking code.
 *
 * !!!NOTICE!!!
 * There are two versions of the locking routines: one for NFSv2 (lm_frlock)
 * and the other for NFSv3 (lm4_frlock).  Changes made to one routine MUST
 * be made to the corresponding code in the other routine.
 *
 * The local locking code is called to maintain the reclocks in the
 * client kernel. This serves two purposes:
 * - The reclocks can be used as a reclock cache by F_SETLK and F_GETLK.
 * - We need the reclocks to perform server crash recovery. Therefore the
 *   reclocks in the client kernel must only contain the locks granted by
 *   the server.  Also, they must be registered using the server's sysid,
 *   not as local locks.
 *
 * The algoritm is:
 *
 * if cmd == F_*GETLK then
 *   if local locking says that another process got the lock then
 *     return this info
 *   else
 *     call server and return result
 *   fi
 * fi
 *
 * if cmd == F_SETLK*  &&  l_type == F_UNLCK then
 *   call server
 *   call local locking
 * fi
 *
 * if cmd == F_SETLK  &&  l_type != F_UNLCK then
 *   call local locking with cmd=F_GETLK
 *   if local locking says that another process got the lock then
 *     return EAGAIN
 *   else
 *     call server
 *     call local locking with cmd=F_SETLK  --- Note, this will always succeed
 *   fi
 * fi
 *
 * if cmd == F_SETLKW  &&  l_type != F_UNLCK then
 *   call server
 *   call local locking -- Note, this will always succeed
 * fi
 *
 * XXX NCR porting issues:
 *	1. If a signal is posted to the calling thread during the
 *		CLNT_CALL(), and flk->l_whence == 2, the VOP_GETATTR() in
 *		convoff() may fail and flk may not be restored correctly.
 *
 */
int
lm_frlock(struct vnode * vp,
	int cmd,
	struct flock * flk,
	int flag,
	off_t offset,
	struct cred * cr,		/* unused */
	netobj *fh)
{
	union {
		nlm_testargs    nta;
		nlm_lockargs    nla;
		nlm_unlockargs  nua;
		nlm_cancargs    nca;
	} arg;

	union {
		nlm_testres	ntr;
		nlm_res		nr;
	} res;

	nlm_lock	alk;
	int		cookie;
	int		proc;
	xdrproc_t	xdr_arg;
	xdrproc_t	xdr_res;
	struct lm_sysid *ls = NULL;
	short		whence;
	int		error = 0;
	int		blocked_rexmit = 0; /* retrans. of blocked request */
	int		signalled;
	struct lm_sleep *lslp = NULL;
	long		oh_bytes[2];
	ulong		off;
	ulong		len;

	lm_debu9(1, "frlck",
		"cmd= %d, offset= %d, type= %d, start= %d, len= %d, whence= %d",
		cmd, offset, flk->l_type, flk->l_start, flk->l_len,
		flk->l_whence);

	if (!lm_caches_created) {
		lm_caches_init();
	}

	mutex_enter(&lm_lck);
	cookie = lm_stat.cookie++;
	mutex_exit(&lm_lck);

#ifdef DEBUG
	if (lm_gc_sysids) {
		lm_free_sysid_table();
	}
#endif

	/*
	 * Convert the offset. It must be restored before returning.
	 * (And not before.) If whence == 2, we need the newest
	 * length of the file. Therefore invalidate the ATTR_CACHE.
	 */
	whence = flk->l_whence;

	if (whence == 2) {
		PURGE_ATTRCACHE(vp);
	}
	if (error = convoff(vp, flk, 0, offset)) {
		lm_debu4(2, "frlck", "convoff  =>  error= %d\n", error);
		return (error);
	}
	/* Reset arg and res. */
	bzero((caddr_t) &arg, sizeof (arg));
	bzero((caddr_t) &res, sizeof (res));

	/*
	 * Initialize alock. The oh field is set to the current time,
	 * followed by the value of the cookie.  For blocking lock
	 * requests, this is so that it can be used to match up a GRANTED
	 * call with a blocked lock reliably (even across reboots).  For
	 * other requests, the oh still needs to be set to something, and
	 * this combination is pretty cheap to use.
	 */
	alk.caller_name = utsname.nodename;
	alk.fh = *fh;
	/* oh_bytes holds the process id and the system name */
	oh_bytes[0] = curproc->p_pid;
	oh_bytes[1] = lm_owner_handle_sys;
	alk.oh.n_len = sizeof (oh_bytes);
	alk.oh.n_bytes = (char *)&oh_bytes;
	alk.svid = curproc->p_pid;
	alk.l_offset = flk->l_start;
	alk.l_len = flk->l_len;

	/* Initialize call-specific parameters. */
	switch (cmd) {
	case F_GETLK:
	case F_O_GETLK:
		proc = NLM_TEST;
		xdr_arg = xdr_nlm_testargs;
		xdr_res = xdr_nlm_testres;
		arg.nta.cookie.n_len = sizeof (cookie);
		arg.nta.cookie.n_bytes = (char *) &cookie;
		arg.nta.exclusive = flk->l_type == F_WRLCK;
		arg.nta.alock = alk;
		break;

	case F_SETLK:
	case F_SETLKW:
		if (flk->l_type == F_UNLCK) {
			proc = NLM_UNLOCK;
			xdr_arg = xdr_nlm_unlockargs;
			xdr_res = xdr_nlm_res;
			arg.nua.cookie.n_len = sizeof (cookie);
			arg.nua.cookie.n_bytes = (char *) &cookie;
			arg.nua.alock = alk;
		} else {
			proc = NLM_LOCK;
			xdr_arg = xdr_nlm_lockargs;
			xdr_res = xdr_nlm_res;
			arg.nla.cookie.n_len = sizeof (cookie);
			arg.nla.cookie.n_bytes = (char *) &cookie;
			arg.nla.block = cmd == F_SETLKW;
			arg.nla.exclusive = flk->l_type == F_WRLCK;
			arg.nla.alock = alk;
			arg.nla.reclaim = FALSE;
			arg.nla.state = 1;
		}
		break;

	default:
		lm_debu4(2, "frlck", "cmd= %d  =>  EINVAL\n", cmd);
		error = EINVAL;
		goto out;
	}

	/*
	 * If we are setting a lock, check that the file is opened
	 * with the correct mode.
	 */
	if (proc == NLM_LOCK) {
		if ((flk->l_type == F_RDLCK && (flag & FREAD) == 0) ||
			(flk->l_type == F_WRLCK && (flag & FWRITE) == 0)) {
			error = EBADF;
			goto out;
		}
	}

	/* Find the lm_sysid */
	rw_enter(&lm_sysids_lock, RW_READER);
	ls = lm_get_sysid(VTOMI(vp)->mi_knetconfig, &VTOMI(vp)->mi_addr,
		VTOMI(vp)->mi_hostname, TRUE, NULL);
	rw_exit(&lm_sysids_lock);

	/*
	 * Before calling server, use the local reclocks as a cache
	 * for NLM_TEST and non-blocking NLM_LOCK calls. Note, there
	 * is no point doing this for blocking NLM_LOCK calls, since
	 * we always want to sleep.
	 */
	if ((proc == NLM_TEST) || (proc == NLM_LOCK && !arg.nla.block)) {
		struct flock    f = *flk;

		lm_debu3(2, "frlck", "Checking reclock cache");

		f.l_pid = ttoproc(curthread)->p_pid;
		f.l_sysid = ls->sysid | LM_SYSID_CLIENT;
		/*
		 * Simulate an F_GETLK call.  Note that the result is
		 * determined by what is left in f, rather than by looking
		 * at the value returned.
		 */
		if ((error = reclock(vp, &f, 0, flag, offset)) != 0) {
			lm_debu4(2, "frlck",
				"fs_frlock(F_GETLK)  =>  error= %d\n", error);
			goto out;
		}
		if (f.l_type != F_UNLCK) {
			/* This lock conflicts with a local lock. */
			if (proc == NLM_TEST) {
				*flk = f;

				lm_debu3(2, "frlck",
			    "fs_frlock(F_GETLK)  =>  NLM_TEST conflict\n");
				error = 0;

				/*
				 * When fcntl is called with F_GETLK,
				 * and whence field is other than 0,
				 * then the call to convoff at exit
				 * modifies the lock fields
				 * incorrectly. So put the whence
				 * field to zero before calling
				 * convoff().
				 */
				if ((cmd == F_GETLK) || (cmd == F_O_GETLK))
					whence = 0;
				goto out;
			} else {
			    lm_debu3(2, "frlck",
				"fs_frlock(F_GETLK)  =>  NLM_LOCK conflict\n");
				error = EAGAIN;
				goto out;
			}
		}
	}

	/*
	 * Check to see if any signals have been posted to our thread.
	 * If so, bail out now.
	 */
	if (lm_sigispending()) {
		lm_debu3(2, "frlck", "signal before lm_callrpc() loop");
		error = EINTR;
		goto out;
	}

	/*
	 * Before unlocking, flush the file to the server.  We do not
	 * want the write to be done after the unlock.
	 *
	 * Also, unregister the lock with the local locking code before
	 * contacting the server.  This avoids a potential race where
	 * another process gets notified that it has been granted a lock
	 * before we can unregister ourselves locally.
	 */
	if (proc == NLM_UNLOCK) {
		error = VOP_PUTPAGE(vp, (offset_t)0, 0, B_INVAL, cr);
		if (error && (error == ENOSPC || error == EDQUOT)) {
			rnode_t *rp = VTOR(vp);
			mutex_enter(&rp->r_statelock);
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
		}
		register_lock_locally(vp, ls, flk, flag, offset);
	}

	/*
	 * If this is a blocking request, allocate an lm_sleep for it.  We
	 * need to do this now, because if we wait until we get a "blocked"
	 * response, there is a window during which the GRANTED call could
	 * come in before we can make note of its arrival.
	 *
	 * Also tell statd to keep track of the server.  This guarantees
	 * the server will eventually get notified if client crashes and
	 * reboots.
	 */
	if (proc == NLM_LOCK && arg.nla.block) {
		lslp = lm_get_sleep(ls, &alk.fh, &alk.oh, alk.l_offset,
				alk.l_len);
		lm_sm_client(ls, lm_get_me());
	}

call_server:
	/*
	 * Call the server. Loop until success or EINTR or EINVAL.
	 * (Done by the continue statement.) After each try, sleep
	 * LM_ERROR_SLP seconds. If server lock-manager is in its
	 * grace period, sleep LM_GRACE seconds. Before each try,
	 * free the res struct (this is also OK the first time).
	 */
	for (;;) {
		signalled = 0;
		xdr_free(xdr_res, (caddr_t) &res);

		error = lm_callrpc(ls, NLM_PROG, NLM_VERS, proc, xdr_arg,
				(caddr_t) &arg, xdr_res, (caddr_t) &res,
				lm_sa.retransmittimeout, LM_RETRY);

		lm_debu4(2, "frlck", "lm_callrpc() returned %d", error);

		/*
		 * If a signal was posted while we were ignoring them,
		 * take note.
		 */
		if (lm_sigispending()) {
			lm_debu3(2, "frlck", "signal after lm_callrpc()");
			signalled = 1;
		}

		/*
		 * It is possible that the request has actually been
		 * granted, but the server's granted response never
		 * made it to us because of this signal or error.
		 * Record this fact so an unlock request will be
		 * generated in nfs_lockrelease() when the file is
		 * closed.
		 */
		if (((signalled == 1) || (error != 0)) &&
			((proc == NLM_LOCK) || (proc == NLM_UNLOCK))) {
			nfs_add_locking_pid(vp, ttoproc(curthread)->p_pid);
		}

		switch (error) {
		case 0:
			if (res.nr.stat.stat == nlm_denied_grace_period) {
				lm_debu3(2, "frlck",
				"Server in grace -- sleeping");
				if (signalled) {
					error = EINTR;
					goto out;
				}
				if (error = lm_delay(LM_GRACE_SLP * HZ)) {
					break;
				}
				continue;
			}
			break;

		case EINVAL:
			/*
			 * A serious error in the RPC call. Do not
			 * call again.
			 */
			lm_debu3(2, "frlck", "RPC call had EINVAL.");
			break;

		case EINTR:
			/*
			 * The RPC call was interrupted.  Do not
			 * call again.
			 */
			lm_debu3(2, "frlck", "RPC call interrupted.");
			break;

		case EIO:
		default:
			/*
			 * We got an error in the RPC call. Wait a
			 * bit, and call again, - unless we got
			 * interrupted.
			 */
			lm_debu4(2, "frlck",
				"RPC call failed: error= %d -- sleeping",
				error);
			if (signalled) {
				/*
				 * There's a possible race condition here.
				 * A signal may have arrived after we timed
				 * out (EIO) but the request actually made
				 * it to the server.  If we were blocking on
				 * a lock, we cancel it just to be safe.
				 */
				if ((proc == NLM_LOCK) && (arg.nla.block)) {
					proc = NLM_CANCEL;
					xdr_arg = xdr_nlm_cancargs;
					xdr_res = xdr_nlm_res;
					mutex_enter(&lm_lck);
					cookie = lm_stat.cookie++;
					mutex_exit(&lm_lck);
					arg.nca.cookie.n_len = sizeof (cookie);
					arg.nca.cookie.n_bytes =
						(char *) &cookie;
					arg.nca.block = cmd == F_SETLKW;
					arg.nca.exclusive =
						flk->l_type == F_WRLCK;
					arg.nca.alock = alk;
					goto call_server;
				}
				error = EINTR;
				goto out;
			}
			if (error = lm_delay(LM_ERROR_SLP * HZ)) {
				break;
			}
			continue;
		}
		/* If we get here, then break the loop. */
		break;
	}


	/*
	 * If no errors, interpret the results.
	 *
	 * First we hack around a bug that some servers have, where they
	 * return an error when a blocked request is retransmitted.
	 * (Note that we can tell a retransmitted blocked request from a
	 * blocking request that was lost, because we know whether we
	 * received a response to the original request.)
	 *
	 * Then we return to the normal result processing.  For NLM_LOCK
	 * calls which have been blocked, we sleep for awhile and wait for
	 * the server to notify us that the lock has been granted.  For
	 * NLM_LOCK or NLM_UNLOCK calls that succeed, the local locking
	 * module must be called to register the result.
	 */
	if (!error) {
		if (proc == NLM_LOCK &&
		    arg.nla.block &&
		    (res.nr.stat.stat == nlm_deadlck ||
			res.nr.stat.stat == nlm_denied_nolocks) &&
		    blocked_rexmit) {
			res.nr.stat.stat = nlm_blocked;
		}

		if ((proc == NLM_LOCK) && (arg.nla.block) &&
				(res.nr.stat.stat == nlm_blocked)) {
			/*
			 * We are blocked.  Wait for an NLM_GRANTED,
			 * unless we've been signalled in which case
			 * we cancel.
			 */
			if (signalled) {
				lm_debu3(2, "frlck",
					"signalled while blocking");
				error = EINTR;
			} else {
				error = lm_waitfor_granted(lslp);
			}

			switch (error) {
			case 0:
				/*
				 * Got the lock from an NLM_GRANTED!
				 * Act as if the NLM_LOCK call
				 * succeeded.
				 */
				res.nr.stat.stat = nlm_granted;
				break;

			case EINTR:
				/*
				 * Sleep was interrupted. Issue an
				 * NLM_CANCEL and exit.
				 */
				lm_debu3(2, "frlck", "EINTR after blocking");
				proc = NLM_CANCEL;
				xdr_arg = xdr_nlm_cancargs;
				xdr_res = xdr_nlm_res;
				mutex_enter(&lm_lck);
				cookie = lm_stat.cookie++;
				mutex_exit(&lm_lck);
				arg.nca.cookie.n_len = sizeof (cookie);
				arg.nca.cookie.n_bytes = (char *) &cookie;
				arg.nca.block = cmd == F_SETLKW;
				arg.nca.exclusive = flk->l_type == F_WRLCK;
				arg.nca.alock = alk;
				goto call_server;

			default:
				lm_debu4(2, "frlck",
					"lm_waitfor_granted returned %d",
					error);
				/*
				 * Sleep timed out. Retransmit
				 * NLM_LOCK call.
				 */
				blocked_rexmit = 1;
				goto call_server;
			}
		}

		switch (proc) {
		case NLM_TEST:
			switch (res.ntr.stat.stat) {
			case nlm_granted:
				flk->l_type = F_UNLCK;
				flk->l_whence = 0;
				error = 0;
				break;

			case nlm_denied:
				flk->l_type =
			    res.ntr.stat.nlm_testrply_u.holder.exclusive ?
					F_WRLCK : F_RDLCK;
				flk->l_whence = 0;
				off =
				    res.ntr.stat.nlm_testrply_u.holder.l_offset;
				len =
				    res.ntr.stat.nlm_testrply_u.holder.l_len;
				if (off > MAXOFF_T) {
					flk->l_start = MAXOFF_T;
					flk->l_end = 0;
				} else if (len > MAXOFF_T) {
					flk->l_start = off;
					flk->l_end = 0;
				} else if ((off + len - 1) > MAXOFF_T) {
					flk->l_start = off;
					flk->l_end = 0;
				} else {
					flk->l_start = off;
					flk->l_len = len;
				}
				flk->l_sysid = 0;
				flk->l_pid =
				    res.ntr.stat.nlm_testrply_u.holder.svid;
				error = 0;
				break;

			case nlm_denied_nolocks:
				error = ENOLCK;
				break;

			default:
				lm_debu4(1, "frlck",
					"unexpected test stat= %d",
					res.nr.stat.stat);
				error = EINVAL;
				break;
			}
			break;

		case NLM_LOCK:
			switch (res.nr.stat.stat) {
			case nlm_granted:
				register_lock_locally(vp, ls, flk, flag,
				    offset);
				/*
				 * Invalidate all the buffer cache for the
				 * vnode. We want to be sure that the read
				 * operation gets the newest data. Tell the
				 * SM to monitor the server.
				 */
				error = VOP_PUTPAGE(vp, (offset_t)0, 0,
						    B_INVAL, cr);
				if (error &&
				    (error == ENOSPC || error == EDQUOT)) {
					rnode_t *rp = VTOR(vp);
					mutex_enter(&rp->r_statelock);
					if (!rp->r_error)
						rp->r_error = error;
					mutex_exit(&rp->r_statelock);
				}
				lm_sm_client(ls, lm_get_me());
				error = 0;
				break;

			case nlm_blocked:
			case nlm_denied:
				if (arg.nla.block) {
					lm_debu4(2, "frlck",
					"unexpected stat= %d for blocking lock",
						res.nr.stat.stat);
				}
				error = EAGAIN;
				break;

			case nlm_denied_nolocks:
			case nlm_deadlck:
				error = lm_stat_to_errno[res.ntr.stat.stat];
				break;

			default:
				lm_debu4(1, "frlck",
					"unexpected lock stat= %d",
					res.nr.stat.stat);
				error = EINVAL;
				break;
			}
			break;

		case NLM_UNLOCK:
			switch (res.nr.stat.stat) {
			case nlm_granted:
				error = 0;
				break;

			case nlm_denied:
				error = EINVAL;	/* shouldn't happen */
				break;

			case nlm_denied_nolocks:
				error = lm_stat_to_errno[res.ntr.stat.stat];
				break;

			default:
				lm_debu4(1, "frlck",
					"unexpected unlock stat= %d",
					res.nr.stat.stat);
				error = EINVAL;
				break;
			}
			break;

		case NLM_CANCEL:
			/*
			 * Set error to EINTR. This was the reason
			 * for sending NLM_CANCEL.
			 */
			error = EINTR;
			break;
		}
	}
	xdr_free(xdr_res, (caddr_t) & res);

out:
	if (convoff(vp, flk, whence, offset)) {
		lm_debu3(2, "frlck", "final convoff failed");
	}

	lm_debu5(2, "frlck", "End: error= %d, type= %d\n", error, flk->l_type);

	if (ls != NULL) {
		lm_rel_sysid(ls);
	}
	if (lslp != NULL) {
		lm_rel_sleep(lslp);
	}

	return (error);
}


/*
 * map NLM4 return status value into an errno value
 */
int
lm_stat4_to_errno[] = {
	0,	/* NLM4_GRANTED */
	EAGAIN,	/* NLM4_DENIED */
	ENOLCK,	/* NLM4_DENIED_NOLOCKS */
	EAGAIN,	/* NLM4_BLOCKED */
	EAGAIN,	/* NLM4_DENIED_GRACE_PERIOD */
	EDEADLK,	/* NLM4_DEADLCK */
	EROFS,	/* NLM4_ROFS */
	ESTALE,	/* NLM4_STALE_FH */
	EFBIG,	/* NLM4_FBIG */
	ENOLCK	/* NLM4_FAILED */
};

/*
 * NLM4 version of frlock.  It does the same thing as lm_frlock, but
 * but uses the NLM4 version of the RPC protocol to call the lock
 * manager.  This is called by nfs3_frlock().
 *
 * !!!NOTICE!!!
 * There are two versions of the locking routines: one for NFSv2 (lm_frlock)
 * and the other for NFSv3 (lm4_frlock).  Changes made to one routine MUST
 * be made to the corresponding code in the other routine.
 *
 */
int
lm4_frlock(struct vnode * vp,
	int cmd,
	struct flock * flk,
	int flag,
	off_t offset,
	struct cred * cr,		/* unused */
	netobj *fh)
{
	union {
		nlm4_testargs    nta;
		nlm4_lockargs    nla;
		nlm4_unlockargs  nua;
		nlm4_cancargs    nca;
	} arg;

	union {
		nlm4_testres	ntr;
		nlm4_res		nr;
	} res;

	nlm4_lock	alk;
	int		cookie;
	int		proc;
	xdrproc_t	xdr_arg;
	xdrproc_t	xdr_res;
	struct lm_sysid *ls = NULL;
	short		whence;
	int		error = 0;
	int		blocked_rexmit = 0; /* retrans. of blocked request */
	int		signalled;
	uint64		off;
	uint64		len;
	struct lm_sleep *lslp = NULL;
	long		oh_bytes[2];

	lm_debu9(1, "frlck4",
		"cmd= %d, offset= %d, type= %d, start= %d, len= %d, whence= %d",
		cmd, offset, flk->l_type, flk->l_start, flk->l_len,
		flk->l_whence);

	if (!lm_caches_created) {
		lm_caches_init();
	}

	mutex_enter(&lm_lck);
	cookie = lm_stat.cookie++;
	mutex_exit(&lm_lck);

#ifdef DEBUG
	if (lm_gc_sysids) {
		lm_free_sysid_table();
	}
#endif

	/*
	 * Convert the offset. It must be restored before returning.
	 * (And not before.) If whence == 2, we need the newest
	 * length of the file. Therefore invalidate the ATTR_CACHE.
	 */
	whence = flk->l_whence;

	if (whence == 2) {
		PURGE_ATTRCACHE(vp);
	}
	if (error = convoff(vp, flk, 0, offset)) {
		lm_debu4(2, "frlck4", "convoff  =>  error= %d\n", error);
		return (error);
	}
	/* Reset arg and res. */
	bzero((caddr_t) &arg, sizeof (arg));
	bzero((caddr_t) &res, sizeof (res));

	/*
	 * Initialize alock. The oh field is set to the current time,
	 * followed by the value of the cookie.  For blocking lock
	 * requests, this is so that it can be used to match up a GRANTED
	 * call with a blocked lock reliably (even across reboots).  For
	 * other requests, the oh still needs to be set to something, and
	 * this combination is pretty cheap to use.
	 */
	alk.caller_name = utsname.nodename;
	alk.fh = *fh;
	/* oh_bytes holds the process id and the system name */
	oh_bytes[0] = curproc->p_pid;
	oh_bytes[1] = lm_owner_handle_sys;
	alk.oh.n_len = sizeof (oh_bytes);
	alk.oh.n_bytes = (char *)&oh_bytes;
	alk.svid = curproc->p_pid;
	alk.l_offset = flk->l_start;
	alk.l_len = flk->l_len;

	/* Initialize call-specific parameters. */
	switch (cmd) {
	case F_GETLK:
	case F_O_GETLK:
		proc = NLMPROC4_TEST;
		xdr_arg = xdr_nlm4_testargs;
		xdr_res = xdr_nlm4_testres;
		arg.nta.cookie.n_len = sizeof (cookie);
		arg.nta.cookie.n_bytes = (char *) &cookie;
		arg.nta.exclusive = flk->l_type == F_WRLCK;
		arg.nta.alock = alk;
		break;

	case F_SETLK:
	case F_SETLKW:
		if (flk->l_type == F_UNLCK) {
			proc = NLMPROC4_UNLOCK;
			xdr_arg = xdr_nlm4_unlockargs;
			xdr_res = xdr_nlm4_res;
			arg.nua.cookie.n_len = sizeof (cookie);
			arg.nua.cookie.n_bytes = (char *) &cookie;
			arg.nua.alock = alk;
		} else {
			proc = NLMPROC4_LOCK;
			xdr_arg = xdr_nlm4_lockargs;
			xdr_res = xdr_nlm4_res;
			arg.nla.cookie.n_len = sizeof (cookie);
			arg.nla.cookie.n_bytes = (char *) &cookie;
			arg.nla.block = cmd == F_SETLKW;
			arg.nla.exclusive = flk->l_type == F_WRLCK;
			arg.nla.alock = alk;
			arg.nla.reclaim = FALSE;
			arg.nla.state = 1;
		}
		break;

	default:
		lm_debu4(2, "frlck4", "cmd= %d  =>  EINVAL\n", cmd);
		error = EINVAL;
		goto out;
	}

	/*
	 * If we are setting a lock, check that the file is opened
	 * with the correct mode.
	 */
	if (proc == NLMPROC4_LOCK) {
		if ((flk->l_type == F_RDLCK && (flag & FREAD) == 0) ||
			(flk->l_type == F_WRLCK && (flag & FWRITE) == 0)) {
			error = EBADF;
			goto out;
		}
	}

	/* Find the lm_sysid */
	rw_enter(&lm_sysids_lock, RW_READER);
	ls = lm_get_sysid(VTOMI(vp)->mi_knetconfig, &VTOMI(vp)->mi_addr,
		VTOMI(vp)->mi_hostname, TRUE, NULL);
	rw_exit(&lm_sysids_lock);

	/*
	 * Before calling server, use the local reclocks as a cache
	 * for NLMPROC4_TEST and non-blocking NLMPROC4_LOCK calls. Note, there
	 * is no point doing this for blocking NLMPROC4_LOCK calls, since
	 * we always want to sleep.
	 */
	if ((proc == NLMPROC4_TEST) ||
		(proc == NLMPROC4_LOCK && !arg.nla.block)) {
		struct flock    f = *flk;

		lm_debu3(2, "frlck4", "Checking reclock cache");

		f.l_pid = ttoproc(curthread)->p_pid;
		f.l_sysid = ls->sysid | LM_SYSID_CLIENT;
		/*
		 * Simulate an F_GETLK call.  Note that the result is
		 * determined by what is left in f, rather than by looking
		 * at the value returned.
		 */
		if ((error = reclock(vp, &f, 0, flag, offset)) != 0) {
			lm_debu4(2, "frlck4",
				"fs_frlock(F_GETLK)  =>  error= %d\n", error);
			goto out;
		}
		if (f.l_type != F_UNLCK) {
			/* This lock conflicts with a local lock. */
			if (proc == NLMPROC4_TEST) {
				*flk = f;

				lm_debu3(2, "frlck4",
			    "fs_frlock(F_GETLK)  =>  NLMPROC4_TEST conflict\n");
				error = 0;

				/*
				 * When fcntl is called with F_GETLK,
				 * and whence field is other than 0,
				 * then the call to convoff at exit
				 * modifies the lock fields
				 * incorrectly. So put the whence
				 * field to zero before calling
				 * convoff().
				 */
				if ((cmd == F_GETLK) || (cmd == F_O_GETLK))
					whence = 0;
				goto out;
			} else {
			    lm_debu3(2, "frlck4",
			    "fs_frlock(F_GETLK)  =>  NLMPROC4_LOCK conflict\n");
				error = EAGAIN;
				goto out;
			}
		}
	}

	/*
	 * Check to see if any signals have been posted to our thread.
	 * If so, bail out now.
	 */
	if (lm_sigispending()) {
		lm_debu3(2, "frlck4", "signal before lm_callrpc() loop");
		error = EINTR;
		goto out;
	}

	/*
	 * Before unlocking, flush the file to the server.  We do not
	 * want the write to be done after the unlock.
	 *
	 * Also, unregister the lock with the local locking code before
	 * contacting the server.  This avoids a potential race where
	 * another process gets notified that it has been granted a lock
	 * before we can unregister ourselves locally.
	 */
	if (proc == NLMPROC4_UNLOCK) {
		error = VOP_PUTPAGE(vp, (offset_t)0, 0, B_INVAL, cr);
		if (error && (error == ENOSPC || error == EDQUOT)) {
			rnode_t *rp = VTOR(vp);
			mutex_enter(&rp->r_statelock);
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
		}
		register_lock_locally(vp, ls, flk, flag, offset);
	}

	/*
	 * If this is a blocking request, allocate an lm_sleep for it.  We
	 * need to do this now, because if we wait until we get a "blocked"
	 * response, there is a window during which the GRANTED call could
	 * come in before we can make note of its arrival.
	 *
	 * Also tell statd to keep track of the server.  This guarantees
	 * the server will eventually get notified if client crashes and
	 * reboots.
	 */
	if (proc == NLMPROC4_LOCK && arg.nla.block) {
		lslp = lm_get_sleep(ls, &alk.fh, &alk.oh, alk.l_offset,
				alk.l_len);
		lm_sm_client(ls, lm_get_me());
	}

call_server:
	/*
	 * Call the server. Loop until success or EINTR or EINVAL.
	 * (Done by the continue statement.) After each try, sleep
	 * LM_ERROR_SLP seconds. If server lock-manager is in its
	 * grace period, sleep LM_GRACE seconds. Before each try,
	 * free the res struct (this is also OK the first time).
	 */
	for (;;) {
		signalled = 0;
		xdr_free(xdr_res, (caddr_t) &res);

		error = lm_callrpc(ls, NLM_PROG, NLM4_VERS, proc, xdr_arg,
				(caddr_t) &arg, xdr_res, (caddr_t) &res,
				lm_sa.retransmittimeout, LM_RETRY);

		lm_debu4(2, "frlck4", "lm_callrpc() returned %d", error);

		/*
		 * If a signal was posted while we were ignoring them,
		 * take note.
		 */
		if (lm_sigispending()) {
			lm_debu3(2, "frlck4", "signal after lm_callrpc()");
			signalled = 1;
		}

		/*
		 * It is possible that the request has actually been
		 * granted, but the server's granted response never
		 * made it to us because of this signal or error.
		 * Record this fact so an unlock request will be
		 * generated in nfs_lockrelease() when the file is
		 * closed.
		 */
		if (((signalled == 1) || (error != 0)) &&
			((proc == NLMPROC4_LOCK) ||
			(proc == NLMPROC4_UNLOCK))) {
			nfs_add_locking_pid(vp, ttoproc(curthread)->p_pid);
		}

		switch (error) {
		case 0:
			if (res.nr.stat.stat == NLM4_DENIED_GRACE_PERIOD) {
				lm_debu3(2, "frlck4",
				"Server in grace -- sleeping");
				if (signalled) {
					error = EINTR;
					goto out;
				}
				if (error = lm_delay(LM_GRACE_SLP * HZ)) {
					break;
				}
				continue;
			}
			break;

		case EINVAL:
			/*
			 * A serious error in the RPC call. Do not
			 * call again.
			 */
			lm_debu3(2, "frlck4", "RPC call had EINVAL.");
			break;

		case EINTR:
			/*
			 * The RPC call was interrupted.  Do not
			 * call again.
			 */
			lm_debu3(2, "frlck4", "RPC call interrupted.");
			break;

		case EIO:
		default:
			/*
			 * We got an error in the RPC call. Wait a
			 * bit, and call again, - unless we got
			 * interrupted.
			 */
			lm_debu4(2, "frlck4",
				"RPC call failed: error= %d -- sleeping",
				error);
			if (signalled) {
				/*
				 * There's a possible race condition here.
				 * A signal may have arrived after we timed
				 * out (EIO) but the request actually made
				 * it to the server.  If we were blocking on
				 * a lock, we cancel it just to be safe.
				 */
				if ((proc == NLMPROC4_LOCK) && arg.nla.block) {
					proc = NLMPROC4_CANCEL;
					xdr_arg = xdr_nlm4_cancargs;
					xdr_res = xdr_nlm4_res;
					mutex_enter(&lm_lck);
					cookie = lm_stat.cookie++;
					mutex_exit(&lm_lck);
					arg.nca.cookie.n_len = sizeof (cookie);
					arg.nca.cookie.n_bytes =
						(char *) &cookie;
					arg.nca.block = cmd == F_SETLKW;
					arg.nca.exclusive =
						flk->l_type == F_WRLCK;
					arg.nca.alock = alk;
					goto call_server;
				}
				error = EINTR;
				goto out;
			}
			if (error = lm_delay(LM_ERROR_SLP * HZ)) {
				break;
			}
			continue;
		}
		/* If we get here, then break the loop. */
		break;
	}


	/*
	 * If no errors, interpret the results.
	 *
	 * First we hack around a bug that some servers have, where they
	 * return an error when a blocked request is retransmitted.
	 * (Note that we can tell a retransmitted blocked request from a
	 * blocking request that was lost, because we know whether we
	 * received a response to the original request.)
	 *
	 * Then we return to the normal result processing.  For
	 * NLMPROC4_LOCK calls which have been blocked, we sleep for awhile
	 * and wait for the server to notify us that the lock has been
	 * granted.  For NLMPROC4_LOCK or NLMPROC4_UNLOCK calls that
	 * succeed, the local locking module must be called to register the
	 * result.
	 */
	if (!error) {
		if (proc == NLMPROC4_LOCK &&
		    arg.nla.block &&
		    (res.nr.stat.stat == NLM4_DEADLCK ||
			res.nr.stat.stat == NLM4_DENIED_NOLOCKS) &&
		    blocked_rexmit) {
			res.nr.stat.stat = NLM4_BLOCKED;
		}

		if ((proc == NLMPROC4_LOCK) && (arg.nla.block) &&
				(res.nr.stat.stat == NLM4_BLOCKED)) {
			/*
			 * We are blocked.  Wait for an NLM4_GRANTED,
			 * unless we've been signalled in which case
			 * we cancel.
			 */
			if (signalled) {
				lm_debu3(2, "frlck4",
					"signalled while blocking");
				error = EINTR;
			} else {
				error = lm_waitfor_granted(lslp);
			}

			switch (error) {
			case 0:
				/*
				 * Got the lock from an NLM4_GRANTED!
				 * Act as if the NLMPROC4_LOCK call
				 * succeeded.
				 */
				res.nr.stat.stat = NLM4_GRANTED;
				break;

			case EINTR:
				/*
				 * Sleep was interrupted. Issue an
				 * NLMPROC4_CANCEL and exit.
				 */
				lm_debu3(2, "frlck4", "EINTR after blocking");
				proc = NLMPROC4_CANCEL;
				xdr_arg = xdr_nlm4_cancargs;
				xdr_res = xdr_nlm4_res;
				mutex_enter(&lm_lck);
				cookie = lm_stat.cookie++;
				mutex_exit(&lm_lck);
				arg.nca.cookie.n_len = sizeof (cookie);
				arg.nca.cookie.n_bytes = (char *) &cookie;
				arg.nca.block = cmd == F_SETLKW;
				arg.nca.exclusive = flk->l_type == F_WRLCK;
				arg.nca.alock = alk;
				goto call_server;

			default:
				lm_debu4(2, "frlck4",
					"lm_waitfor_granted returned %d",
					error);
				/*
				 * Sleep timed out. Retransmit
				 * NLMPROC4_LOCK call.
				 */
				blocked_rexmit = 1;
				goto call_server;
			}
		}

		switch (proc) {
		case NLMPROC4_TEST:
			switch (res.ntr.stat.stat) {
			case NLM4_GRANTED:
				flk->l_type = F_UNLCK;
				flk->l_whence = 0;
				error = 0;
				break;

			case NLM4_DENIED:
				flk->l_type = res.ntr.stat.nlm4_testrply_u.
					holder.exclusive ? F_WRLCK : F_RDLCK;
				flk->l_whence = 0;
				/*
				 * convert the 64bit length and offset to 32bit
				 * values in a deterministic way.
				 */
				off = res.ntr.stat.nlm4_testrply_u.holder.
					l_offset;
				len = res.ntr.stat.nlm4_testrply_u.holder.
					l_len;
				if (off + len <= (uint64)MAXOFF_T) {
					/* both values fit */
					flk->l_start = (long)off;
					flk->l_len = (long) len;
				} else if (off < (uint64)MAXOFF_T) {
					/* len is too long */
					flk->l_start = (long)off;
					flk->l_len = 0;	/* to end of file */
				} else {
					/* offset is too big */
					flk->l_start = MAXOFF_T;
					flk->l_len = 0;
				}
				flk->l_sysid = 0;
				flk->l_pid = res.ntr.stat.nlm4_testrply_u.
						holder.svid;
				error = 0;
				break;

			case NLM4_DENIED_NOLOCKS:
			case NLM4_BLOCKED:
			case NLM4_DENIED_GRACE_PERIOD:
			case NLM4_DEADLCK:
			case NLM4_ROFS:
			case NLM4_STALE_FH:
			case NLM4_FBIG:
			case NLM4_FAILED:
				error = lm_stat4_to_errno[res.ntr.stat.stat];
				break;

			default:
				lm_debu4(1, "frlck4",
					"unexpected test stat= %d",
					res.nr.stat.stat);
				error = EINVAL;
				break;
			}
			break;

		case NLMPROC4_LOCK:
			switch (res.nr.stat.stat) {
			case NLM4_GRANTED:
				register_lock_locally(vp, ls, flk, flag,
				    offset);
				/*
				 * Invalidate all the buffer cache for the
				 * vnode. We want to be sure that the read
				 * operation gets the newest data. Tell the
				 * SM to monitor the server.
				 */
				error = VOP_PUTPAGE(vp, (offset_t)0, 0,
						    B_INVAL, cr);
				if (error &&
				    (error == ENOSPC || error == EDQUOT)) {
					rnode_t *rp = VTOR(vp);
					mutex_enter(&rp->r_statelock);
					if (!rp->r_error)
						rp->r_error = error;
					mutex_exit(&rp->r_statelock);
				}
				lm_sm_client(ls, lm_get_me());
				error = 0;
				break;

			case NLM4_BLOCKED:
			case NLM4_DENIED:
				if (arg.nla.block) {
					lm_debu4(2, "frlck4",
					"unexpected stat= %d for blocking lock",
						res.nr.stat.stat);
				}
				error = EAGAIN;
				break;

			case NLM4_DENIED_NOLOCKS:
			case NLM4_DEADLCK:
			case NLM4_DENIED_GRACE_PERIOD:
			case NLM4_ROFS:
			case NLM4_STALE_FH:
			case NLM4_FBIG:
			case NLM4_FAILED:
				error = lm_stat4_to_errno[res.ntr.stat.stat];
				break;

			default:
				lm_debu4(1, "frlck4",
					"unexpected lock stat= %d",
					res.nr.stat.stat);
				error = EINVAL;
				break;
			}
			break;

		case NLMPROC4_UNLOCK:
			switch (res.nr.stat.stat) {
			case NLM4_GRANTED:
				error = 0;
				break;

			case NLM4_DENIED:
			case NLM4_FAILED:
				error = EINVAL;	/* shouldn't happen */
				break;

			case NLM4_DENIED_NOLOCKS:
			case NLM4_ROFS:
			case NLM4_FBIG:
			case NLM4_STALE_FH:
				error = lm_stat4_to_errno[res.ntr.stat.stat];
				break;

			default:
				lm_debu4(1, "frlck4",
					"unexpected unlock stat= %d",
					res.nr.stat.stat);
				error = EINVAL;
				break;
			}
			break;

		case NLMPROC4_CANCEL:
			/*
			 * Set error to EINTR. This was the reason
			 * for sending NLMPROC4_CANCEL.
			 */
			error = EINTR;
			break;
		}
	}
	xdr_free(xdr_res, (caddr_t) & res);

out:
	if (convoff(vp, flk, whence, offset)) {
		lm_debu3(2, "frlck4", "final convoff failed");
	}

	lm_debu5(2, "frlck4", "End: error= %d, type= %d\n", error, flk->l_type);

	if (ls != NULL) {
		lm_rel_sysid(ls);
	}
	if (lslp != NULL) {
		lm_rel_sleep(lslp);
	}

	return (error);
}

/*
 * After a lock is successfully obtained from the server, register it
 * locally.  This should always succeed because the server told us we could
 * have the lock.
 */
static void
register_lock_locally(vnode_t *vp, struct lm_sysid *ls, struct flock *flk,
		int flag, off_t offset)
{
	int oldsysid;
	int error;

	flk->l_pid = ttoproc(curthread)->p_pid;
	oldsysid = flk->l_sysid;
	ASSERT(ls->sysid != 0);
	flk->l_sysid = ls->sysid | LM_SYSID_CLIENT;
	error = reclock(vp, flk, SETFLCK, flag, offset);
#ifdef DEBUG
	if (error != 0) {
		cmn_err(CE_WARN, "register_lock_locally failed");
		cmn_err(CE_CONT,
			"error %d, vp 0x%x, pid %ld, sysid 0x%lx",
			error, (int)vp, flk->l_pid, flk->l_sysid);
		cmn_err(CE_CONT, "type %d off 0x%lx len 0x%lx\n",
			flk->l_type, flk->l_start, flk->l_len);
		(void) reclock(vp, flk, 0, flag, offset);
		cmn_err(CE_CONT,
	    "blocked by pid %ld sysid 0x%lx type %d off 0x%lx len 0x%lx\n",
		    flk->l_pid, flk->l_sysid, flk->l_type, flk->l_start,
		    flk->l_len);
	}
#endif
	flk->l_sysid = oldsysid;
}
