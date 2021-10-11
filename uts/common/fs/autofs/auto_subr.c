/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)auto_subr.c 1.33     95/05/09 SMI"

#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <sys/t_lock.h>
#include <sys/errno.h>
#include <sys/thread.h>
#include <sys/tiuser.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include <sys/fs/autofs.h>
#include <sys/fs/autofs_prot.h>

extern vnodeops_t auto_vnodeops;
extern void sigintr();
extern void sigunintr();

#ifdef	AUTODEBUG
extern int autodebug;
#endif	AUTODEBUG
static int auto_thr_success = 0;
static int auto_thr_fail = 0;

/*
 * Unmount_list is a list of device ids that need to be
 * unmounted. Each Corresponds to a mounted file system.
 * A mount hierarchy may have mounts underneath it, but
 * this list could be empty if any one of them is busy.
 * But we do keep a count of underlying mounts. This is
 * because if the daemon is able to unmount everything
 * but the kernel times out, then we will have a directory
 * hierarchy that will need to be removed. So when this
 * count is zero we know that we can safely remove the
 * hierarchy
 */
static umntrequest *unmount_list;
static int mnts_in_hierarchy;

kmutex_t autonode_list_lock;
struct autonode *autonode_list;
kmutex_t autonode_count_lock;
int anode_cnt, makeautonode_count, freeautonode_count;

vnode_t	*makeautonode(vtype_t, vfs_t *, cred_t *);
void	freeautonode(autonode_t *anp);
int	do_mount(vnode_t *, char *, cred_t *);
int	autodir_lookup(vnode_t *, char *, vnode_t **, cred_t *, int);
int	auto_direnter(autonode_t *, autonode_t *);
void	do_unmount(void);
bool_t	xdr_umntrequest(XDR *, umntrequest *);
bool_t	xdr_umntrequest_encode(XDR *, umntrequest *);
bool_t	xdr_mntrequest(XDR *, mntrequest *);
bool_t	xdr_mntres(XDR *, mntres *);
bool_t	xdr_umntres(XDR *, umntres *);

static void	call_automountd(struct auto_callargs *);
static int	send_unmount_request(autonode_t *);
static void	rm_autonode(autonode_t *);
static void	rm_hierarchy(autonode_t *);
static void	unmount_hierarchy(autonode_t *);
static void	make_unmount_list(autonode_t *);
static int	get_hierarchical_mounts(vfs_t *);


/*
 * Make a new autofs node.
 */
vnode_t *
makeautonode(
	vtype_t type,
	vfs_t *vfsp,
	cred_t *cred
)
{
	autonode_t *ap;
	vnode_t *vp;
	static u_short nodeid = 2;

	ap = (autonode_t *) kmem_zalloc(sizeof (*ap), KM_SLEEP);
	vp = antovn(ap);

	ap->an_uid	= cred->cr_uid;
	ap->an_gid	= cred->cr_gid;
	ap->an_size	= 2; /* for . and .. dir enteries */
	vp->v_count	= 1;
	vp->v_op	= &auto_vnodeops;
	vp->v_type	= type;
	vp->v_rdev	= 0;
	vp->v_data	= (caddr_t) ap;
	vp->v_vfsp	= vfsp;
	mutex_enter(&autonode_count_lock);
	makeautonode_count++;
	anode_cnt++;
	vntoan(vp)->an_nodeid = nodeid++;
	mutex_exit(&autonode_count_lock);
	rw_init(&ap->an_rwlock, "autonode rwlock", RW_DEFAULT, NULL);
	cv_init(&ap->an_cv_mount, "autofs mount cv", CV_DEFAULT, NULL);
	cv_init(&ap->an_cv_umount, "autofs umount cv", CV_DEFAULT, NULL);
	mutex_init(&ap->an_lock, "autonode lock", MUTEX_DEFAULT, NULL);

	return (vp);
}

void
freeautonode(
	autonode_t *anp
)
{
	mutex_enter(&autonode_count_lock);
	freeautonode_count++;
	anode_cnt--;
	mutex_exit(&autonode_count_lock);
	rw_destroy(&anp->an_rwlock);
	cv_destroy(&anp->an_cv_mount);
	cv_destroy(&anp->an_cv_umount);
	mutex_destroy(&anp->an_lock);
	kmem_free((caddr_t) anp, sizeof (*anp));
}


int autofs_debug;

int
do_mount(
	vnode_t *vp,
	char *name,
	cred_t *cr
)
{
	autonode_t *anp = vntoan(vp);
	struct auto_callargs *args;
	k_sigset_t smask;
	int namelen;
	int error;
	struct autoinfo *aip = vfstoai(vp->v_vfsp);

#ifdef AUTODEBUG

	auto_dprint(autodebug, 3,
		"do_mount: path=%s name=%s\n",
		aip->ai_path, name);
#endif
	/*
	 * XXX : When doing a do_mount for /home/foo it will
	 * also block do_mount for /home/bar because the flags
	 * are set in the autonode /home. So when the mount
	 * of /home/foo returns it will wake up the blocked
	 * mount of /home/bar with the same error as returned
	 * for /home/foo
	 */

	error = 0;
	ASSERT(MUTEX_HELD(&anp->an_lock));
	if (anp->an_mntflags & MF_INPROG) {	/* mount in progress */
		sigintr(&smask, 1);
		while (anp->an_mntflags & MF_INPROG) {
			anp->an_mntflags |= MF_WAITING_MOUNT;
			if (!cv_wait_sig(&anp->an_cv_mount, &anp->an_lock)) {
				error = EINTR;
				break;
			}
		}
		sigunintr(&smask);
		if (error == EINTR) {
#ifdef AUTODEBUG
	auto_dprint(autodebug, 3, "do_mount: (%s %s) return error=%d\n",
		aip->ai_path, name, error);
#endif
			return (error);
		}

		if (anp->an_mntflags & MF_MNTPNT) {
			/*
			 * Direct mount for which we're waiting and just
			 * finished. Return the same error.
			 */
			error = anp->an_error;
		} else {
			/*
			 * Indirect mount, we might be waiting for the same
			 * mount, or a different mount (because where we do
			 * locking. Do the lookup again to determine if
			 * our mount has already taken place; if not it will
			 * end up calling this routine again.
			 */
			error = EAGAIN;
		}
#ifdef AUTODEBUG
	auto_dprint(autodebug, 3, "do_mount: (%s %s) return error=%d\n",
		aip->ai_path, name, error);
#endif
		return (error);
	}

	anp->an_mntflags |= MF_INPROG;
	(void) strcpy(aip->ai_current, name);
	mutex_exit(&anp->an_lock);

	args = (struct auto_callargs *) kmem_alloc(sizeof (*args), KM_SLEEP);
	VN_HOLD(vp);
	args->ac_vp = vp;
	namelen = strlen(name) + 1;
	args->ac_name = (char *) kmem_alloc(namelen, KM_SLEEP);
	(void) strcpy(args->ac_name, name);
	crhold(cr);
	args->ac_cred = cr;
	/*
	 * Indicates thread running call_automountd() to exit when done.
	 */
	args->ac_thr_exit = 1;

	if (thread_create(NULL, NULL, call_automountd, (caddr_t)args,
		0, &p0, TS_RUN, 60) == NULL) {
#ifdef AUTODEBUG
			cmn_err(CE_WARN, "do_mount: thread_create failed");
#endif
			auto_thr_fail++;
			/*
			 * Indicates call_automountd() NOT to thread_exit()
			 * when done.
			 */
			args->ac_thr_exit = 0;
			call_automountd(args);
			mutex_enter(&anp->an_lock);
			error = anp->an_error;
#ifdef AUTODEBUG
	auto_dprint(autodebug, 3, "do_mount: (%s %s) return error=%d\n",
		aip->ai_path, name, error);
#endif
			return (error);
	}

	auto_thr_success++;
	mutex_enter(&anp->an_lock);
	error = anp->an_error;
	if (anp->an_mntflags & MF_INPROG) {
		anp->an_mntflags |= MF_WAITING_MOUNT;
		sigintr(&smask, 1);
		if (!cv_wait_sig(&anp->an_cv_mount, &anp->an_lock))
			error = EINTR;
		else
			error = anp->an_error;
		sigunintr(&smask);
	}
#ifdef AUTODEBUG
	auto_dprint(autodebug, 3, "do_mount: (%s %s) return error=%d\n",
		aip->ai_path, name, error);
#endif
	return (error);
}


static void
call_automountd(
	struct auto_callargs *args
)
{
	struct autoinfo *aip;
	autonode_t *anp;
	cred_t *cr;
	CLIENT *clnt;
	enum clnt_stat status;
	struct timeval timeout;
	int retrans = INT_MAX;	/* forever ? */
	mntrequest request;
	mntres result;
	int namelen;
	int error;
	int thr_exit = args->ac_thr_exit;	/* kill thread on exit? */

	aip = vfstoai((args->ac_vp)->v_vfsp);
	cr = args->ac_cred;
	anp = vntoan(args->ac_vp);

#ifdef AUTODEBUG
	auto_dprint(autodebug, 3,
		"call_automountd: path=%s name=%s\n",
		aip->ai_path, args->ac_name);
#endif

	error = clnt_tli_kcreate(&aip->ai_knconf, &aip->ai_addr,
		AUTOFS_PROG, AUTOFS_VERS, 0, retrans, cr, &clnt);
	if (error) {
		cmn_err(CE_WARN, "autofs: clnt_tli_kcreate: error %d\n",
			error);
		goto done;
	}

	timeout.tv_sec = aip->ai_rpc_to;
	timeout.tv_usec = 0;

	if (aip->ai_direct)
		request.name = aip->ai_path;
	else
		request.name	= args->ac_name;

	request.map	= aip->ai_map;
	request.opts	= aip->ai_opts;
	request.path	= aip->ai_path;

	status = clnt_call(clnt, AUTOFS_MOUNT,
		(xdrproc_t) xdr_mntrequest, (caddr_t) &request,
		(xdrproc_t) xdr_mntres, (caddr_t) &result,
		timeout);

	auth_destroy(clnt->cl_auth);	/* drop the authenticator */
	clnt_destroy(clnt);		/* drop the client handle */

	switch (status) {
	case RPC_SUCCESS:
		error = result.status;
		break;
	case RPC_INTR:
		error = EINTR;
		break;
	case RPC_TIMEDOUT:
		/*
		 * Shouldn't get here since we're
		 * supposed to retry forever (almost).
		 */
		cmn_err(CE_WARN, "autofs: mount request timed out\n");
		error = ETIMEDOUT;
		break;
	default:
		cmn_err(CE_WARN, "autofs: %s\n", clnt_sperrno(status));
		error = ENOENT;
		break;
	}

done:
	anp->an_ref_time = hrestime.tv_sec;

	mutex_enter(&anp->an_lock);
	anp->an_error = error;
	anp->an_mntflags &= ~MF_INPROG;
	aip->ai_current[0] = '\0';
	if (anp->an_mntflags & MF_WAITING_MOUNT) {
		cv_broadcast(&anp->an_cv_mount);
		anp->an_mntflags &= ~MF_WAITING_MOUNT;
	}
	if (!error)
		anp->an_mntflags |= MF_MOUNTED;
	mutex_exit(&anp->an_lock);

#ifdef AUTODEBUG
	auto_dprint(autodebug, 3, "call_automountd: (%s %s) error=%d\n",
		aip->ai_path, args->ac_name, error);
#endif
	/*
	 * Now, release the vnode and free the credential
	 * structure.
	 */
	VN_RELE(args->ac_vp);
	crfree(args->ac_cred);
	/*
	 * Free the argument passed in here, since my caller who
	 * allocated it may no longer be around.
	 */
	namelen = strlen(args->ac_name) + 1;
	kmem_free((caddr_t)args->ac_name, namelen);
	kmem_free((caddr_t)args, sizeof (*args));

	/*
	 * if call was done in the context of a new thread, this thread
	 * should now kill itself, otherwise call should return to caller.
	 */
	if (thr_exit) {
		thread_exit();
		/* NOTREACHED */
	}
}


/* ARGSUSED */
int
autodir_lookup(
	vnode_t *dvp,
	char *nm,
	vnode_t **vpp,
	cred_t *cred,
	int special
)
{
	autonode_t *ap, *dap;

	*vpp = NULL;
	if (dvp->v_type != VDIR)
		return (ENOTDIR);
	dap = vntoan(dvp);

	ASSERT(RW_LOCK_HELD(&dap->an_rwlock));
	for (ap = dap->an_dirents; ap; ap = ap->an_next) {
		if (strcmp(ap->an_name, nm) == 0) {
			if (!special)
				ap->an_ref_time = hrestime.tv_sec;
			*vpp = antovn(ap);
			VN_HOLD(*vpp);
			return (0);
		}
	}
	return (ENOENT);
}


int
auto_direnter(
	autonode_t *dap,
	autonode_t *ap
)
{
	autonode_t *cap, **spp;
	u_short offset = 0;
	u_short diff;

	rw_enter(&dap->an_rwlock, RW_WRITER);
	cap = dap->an_dirents;
	if (cap == NULL) {
		/*
		 * offset = 0 for . and  offset = 1 for ..
		 */
		spp = &dap->an_dirents;
		offset = 2;
	}

	/*
	 * even after the slot is determined, this
	 * loop must go on to ensure that the name
	 * does not exist
	 */
	for (; cap; cap = cap->an_next) {
		if (strcmp(cap->an_name, ap->an_name) == 0) {
			rw_exit(&dap->an_rwlock);
			return (EEXIST);
		}
		if (cap->an_next != NULL) {
			diff = cap->an_next->an_offset - cap->an_offset;
			ASSERT(diff != 0);
			if ((diff > 1) && (offset == 0)) {
				offset = cap->an_offset + 1;
				spp = &cap->an_next;
			}
		} else if (offset == 0) {
			offset = cap->an_offset + 1;
			spp = &cap->an_next;
			}
	}
	ap->an_offset	= offset;
	ap->an_next	= *spp;
	*spp		= ap;
	mutex_enter(&dap->an_lock);
	dap->an_size++;
	mutex_exit(&dap->an_lock);

	rw_exit(&dap->an_rwlock);
	return (0);
}

static int
send_unmount_request(
	autonode_t *ap
)
{
	umntrequest *ul;
	struct autoinfo *aip;
	vfs_t *vfsp;
	int retrans = INT_MAX;	/* forever ? */
	int error;
	CLIENT *clnt;
	struct timeval timeout;
	enum clnt_stat status;
	umntres result;

	if (unmount_list == NULL)
		return (0);

	ul = unmount_list;

	vfsp = (antovn(ap))->v_vfsp;
	aip = vfstoai(vfsp);

	error = clnt_tli_kcreate(&aip->ai_knconf, &aip->ai_addr,
			AUTOFS_PROG, AUTOFS_VERS,
			0, retrans, CRED(), &clnt);
	if (error) {
		cmn_err(CE_WARN,
			"autofs: clnt_tli_kcreate: error %d\n",
			error);
		goto done;
	}

	timeout.tv_sec = aip->ai_rpc_to;
	timeout.tv_usec = 0;

	status = clnt_call(clnt, AUTOFS_UNMOUNT,
			(xdrproc_t) xdr_umntrequest,
			(caddr_t) ul, (xdrproc_t) xdr_umntres,
			(caddr_t) &result, timeout);
	auth_destroy(clnt->cl_auth);
	clnt_destroy(clnt);

	switch (status) {
	case RPC_SUCCESS:
		error = result.status;
		break;
	case RPC_INTR:
		error = EINTR;
		break;
	case RPC_TIMEDOUT:
		/*
		 * Shouldn't get here since we're
		 * supposed to retry forever (almost).
		 */
		cmn_err(CE_WARN, "autofs: unmount request timed out\n");
		error = ETIMEDOUT;
		break;
	default:
		cmn_err(CE_WARN, "autofs: %s\n",
			clnt_sperrno(status));
		error = ENOENT;
		break;
	}

done:
	while (ul) {
		unmount_list = unmount_list->next;
		kmem_free((caddr_t) ul, sizeof (*ul));
		ul = unmount_list;
	}
	unmount_list = NULL;
	return (error);
}

static void
rm_autonode(
	autonode_t *ap
)
{
	vnode_t *vp = antovn(ap);
	autonode_t *dap;
	autonode_t **app, *cap;


	if (vp->v_vfsmountedhere != NULL) {
#ifdef AUTODEBUG
		if (autofs_debug)
			cmn_err(CE_WARN, "autofs: rm_autonode: "
				"persistent mount: %s",
				ap->an_name);
#endif
		return;
	}

	/*
	 * remove the node only if is an indirect mount
	 * or offset node for a direct mount. ie. dont
	 * remove the node if it was the direct mount
	 * mountpoint.
	 */
	if (ap->an_mntflags & MF_MNTPNT)
		return;

	dap = ap->an_parent;

	ASSERT((dap != ap) && (dap != NULL));

	if ((app = &dap->an_dirents) == NULL)
		cmn_err(CE_PANIC,
			"rm_autonode: null directory list in parent 0x%x",
			(int) dap);

	/*
	 * do not need to hold a RW lock because we are
	 * holding the RW LOCK for the top of the hierarchy
	 * and nobobdy can enter
	 */
	for (;;) {
		cap = *app;
		if (cap == NULL) {
			cmn_err(CE_PANIC,
				"rm_autonode: No entry for %x\n", (int)ap);
		}
		if (cap == ap)
			break;
		app = &cap->an_next;
	}
	*app = cap->an_next;
	mutex_enter(&dap->an_lock);
	dap->an_size--;
	mutex_exit(&dap->an_lock);

	/* the autonode had a pointer to parent so vn_rele it */

	VN_RELE(antovn(dap));

	VN_HOLD(antovn(ap));
	mutex_enter(&ap->an_lock);
	ap->an_size -= 2;
	mutex_exit(&ap->an_lock);
	VN_RELE(antovn(ap));
}

/*
 * Check the reference counts on an autonode and
 * sub-autonodes to see if there's a process that's
 * holding a reference.  If so, then return 1.
 */
static int
autofs_busy(ap)
	struct autonode *ap;
{
	int dirs = ap->an_size - 2;	/* don't count "." and ".." */
	int refs = antovn(ap)->v_count;

	if (ap->an_mntflags & MF_MOUNTED)
		refs--;

	if (dirs && refs > dirs)
		return (1);

	for (ap = ap->an_dirents; ap; ap = ap->an_next)
		if (autofs_busy(ap))
			return (1);

	return (0);
}

static void
rm_hierarchy(
	autonode_t *ap
)
{
	autonode_t *child, *next;

	/* LINTED lint thinks next may be used before set */
	for (child = ap->an_dirents; child; child = next) {
		next = child->an_next;
		rm_hierarchy(child);
	}

	rm_autonode(ap);
}

static void
unmount_hierarchy(
	autonode_t *ap
)
{
	autonode_t *dap;
	int res = 0;
	struct autoinfo *aip;
	long time_now = hrestime.tv_sec;

	unmount_list = NULL;
	mnts_in_hierarchy = 0;

#ifdef AUTODEBUG
	if (autofs_debug)
		printf("unmount_hierarchy: autonode: %s\n", ap->an_name);
#endif
	aip = vfstoai(antovn(ap)->v_vfsp);

	if (ap->an_ref_time + aip->ai_mount_to > time_now)
		return;
	if (ap->an_mntflags & (MF_INPROG | MF_WAITING_MOUNT))
		return;

	if (autofs_busy(ap)) {
		ap->an_ref_time = time_now;
		return;
	}

	make_unmount_list(ap);
	if (mnts_in_hierarchy == 0) {
		ASSERT(unmount_list == NULL);

		dap = ap->an_parent;
		rw_enter(&dap->an_rwlock, RW_WRITER);
		/*
		 * it is important to grab this lock before
		 * checking if somebody is waiting to
		 * prevent anybody from doing a lookup
		 * before we get this lock
		 */
		rm_hierarchy(ap);
		rw_exit(&dap->an_rwlock);
	}

	if (unmount_list) {
		/*
		 * if it is a direct mount without any offset
		 * set the is direct flag in the unmount request
		 * It help the daemon to determine that it is a
		 * direct mount and so it does not attach any spaces
		 * to the end of the path for unmounting
		 */
		if ((ap->an_mntflags & MF_MNTPNT) &&
		    (ap->an_dirents == NULL)) {
			umntrequest *ur;
			for (ur = unmount_list; ur; ur = ur->next)
				ur->isdirect = 1;
		}

		mutex_enter(&ap->an_lock);
		ap->an_mntflags |= MF_UNMOUNTING;
		mutex_exit(&ap->an_lock);

		res = send_unmount_request(ap);

		/*
		 * the result can never be a timeout. send_unmount_req
		 * ensures that. The trouble with timeout is that we
		 * do not know how far the unmount has succeeded if
		 * at all. Therefore we need to keep the MF_UNMOUNTING
		 * flag. This will not let any process to enter the
		 * hierarchy. So in send_unmount_request we keep
		 * trying until we get a response other than timeout
		 */
		if (res) {
			/*
			 * Not successful, so wakeup every
			 * thread sleeping on this hierarchy
			 */
			mutex_enter(&ap->an_lock);
			ap->an_mntflags &= ~MF_UNMOUNTING;
			if (ap->an_mntflags & MF_WAITING_UMOUNT) {
				/*
				 * threads are waiting, but since
				 * the unmount did not succeed, we
				 * dont want them to attempt a re-mount
				 */
				ap->an_mntflags |= MF_DONTMOUNT;
				cv_broadcast(&ap->an_cv_umount);
			}
			ap->an_ref_time = time_now;
			mutex_exit(&ap->an_lock);

		} else {
			/* success unmount */
			dap = ap->an_parent;
			rw_enter(&dap->an_rwlock, RW_WRITER);
			/*
			 * it is important to grab this lock before
			 * checking if somebody is waiting to
			 * prevent anybody from doing a lookup
			 * before we get this lock
			 */
			mutex_enter(&ap->an_lock);
			ap->an_mntflags &= ~MF_UNMOUNTING;
			ap->an_mntflags &= ~MF_MOUNTED;
			if (ap->an_mntflags & MF_WAITING_UMOUNT) {
				cv_broadcast(&ap->an_cv_umount);
				mutex_exit(&ap->an_lock);
			} else {
				mutex_exit(&ap->an_lock);
				rm_hierarchy(ap);
			}
			rw_exit(&dap->an_rwlock);
		}
	}
}

void
do_unmount(void)
{
	autonode_t *ap, *next;
	autonode_t *root_ap;

	for (;;) {			/* loop forever */
		delay(120 * HZ);

#ifdef AUTODEBUG
		if (autofs_debug)
			printf("do_unmount: wakeup\n");
#endif
		/*
		 * The unmount cannot hold the
		 * autonode_list_lock during unmounts
		 * because one of those unmounts might
		 * be an autofs if we have hierarchical
		 * autofs mounts.
		 * Hence we use a simple marking scheme
		 * to make sure we check all the autonodes
		 * for unmounting.
		 * Start by marking them all "not checked"
		 */
		mutex_enter(&autonode_list_lock);
		for (ap = autonode_list; ap; ap = ap->an_next)
			ap->an_mntflags &= ~(MF_CHECKED);
		mutex_exit(&autonode_list_lock);

		/*
		 * Now check the list, starting from the
		 * beginning each pass until we make
		 * a complete pass without finding
		 * any not checked.
		 */
		for (;;) {
			mutex_enter(&autonode_list_lock);
			for (ap = autonode_list; ap; ap = ap->an_next) {
				if (!(ap->an_mntflags & MF_CHECKED))
					break;
			}
			mutex_exit(&autonode_list_lock);
			if (ap == NULL)
				break;

			/*
			 * XXX this should change. The check for
			 * MF_INPROG || MF_WAITING_MOUNT flag should
			 * be set at one level lower.
			 * But till we fix do_mount this remains
			 */
			mutex_enter(&ap->an_lock);
			if (ap->an_mntflags & (MF_INPROG | MF_WAITING_MOUNT)) {
				ap->an_mntflags |= MF_CHECKED;
				mutex_exit(&ap->an_lock);
				continue;
			}
			mutex_exit(&ap->an_lock);

			/*
			 *if it is a direct mount then treat it
			 * as a hierarchy, otherwise go one more
			 * level and treat that as a hierarchy
			 */
			if (ap->an_mntflags & MF_MNTPNT) {
				/* a direct mount */
				if (ap->an_dirents == NULL) {
					/*
					 * must be direct mount
					 * without offsets, so,
					 * only go further if
					 * something is mounted
					 * here
					 */
					if ((antovn(ap))->v_vfsmountedhere)
						unmount_hierarchy(ap);
				} else
					/*
					 * direct mount with offsets
					 */
					unmount_hierarchy(ap);
			} else if (ap->an_dirents) {
				for (root_ap = ap->an_dirents; root_ap;
					/* LINTED bogus used before set */
					root_ap = next) {
					next = root_ap->an_next;
					unmount_hierarchy(root_ap);
				}
			}
			ap->an_mntflags |= MF_CHECKED;
		}

	}
}

static void
make_unmount_list(
	autonode_t *auto_dir
)
{
	vnode_t *vp;
	int error = 0;
	autonode_t *ap;

#ifdef AUTODEBUG
	if (autofs_debug)
		printf("make_unmount_list: autonode: %s\n", auto_dir->an_name);
#endif
	vp = antovn(auto_dir);

	if (auto_dir->an_dirents && !(vp->v_vfsmountedhere)) {
		for (ap = auto_dir->an_dirents; ap; ap = ap->an_next)
			make_unmount_list(ap);
	}
	/*
	 * check if any other vfs is rooted here
	 * if yes then add this path to the
	 * unmount list and recurse again
	 */
	if (vp->v_vfsmountedhere) {
		mutex_enter(&vfslist);
		error = get_hierarchical_mounts(vp->v_vfsmountedhere);
		mutex_exit(&vfslist);
		if (error) {
			umntrequest *temp;
			while (unmount_list) {
				temp = unmount_list;
				unmount_list = temp->next;
				kmem_free((caddr_t) temp, sizeof (*temp));
			}
		}
	}
}


static int
get_hierarchical_mounts(
	vfs_t *vfsp
)
{
	vfs_t *cvfs;
	vnode_t *covered_vp = NULL;
	umntrequest *ul;
	vnode_t *root_vp;
	int error;

#ifdef AUTODEBUG
	if (autofs_debug)
		printf("get_hierarchical_mounts: checking dev=%x\n",
			vfsp->vfs_dev);
#endif

	if (vfsp->vfs_nsubmounts) {
		for (cvfs = rootvfs->vfs_next; cvfs; cvfs = cvfs->vfs_next) {
			if (vfsp != cvfs) {
				VN_HOLD(cvfs->vfs_vnodecovered);
				covered_vp = cvfs->vfs_vnodecovered;
				if (covered_vp->v_vfsp == vfsp) {
					/* something else is rooted here */
					VN_RELE(covered_vp);
					error = get_hierarchical_mounts(cvfs);
					if (error)
						return (error);
				} else
					VN_RELE(covered_vp);
			}
		}
	}
	mnts_in_hierarchy++;

	/*
	 * XXX check for VFS_BUSY can come here
	 * put it in the FRONT of the unmount list
	 */

	ul = (umntrequest *)
		kmem_alloc(sizeof (*ul), KM_SLEEP);
	ul->next = NULL;
	ul->devid = vfsp->vfs_dev;
	ul->isdirect = 0;

	if (error = VFS_ROOT(vfsp, &root_vp)) {
		cmn_err(CE_WARN,
			"get_hierarchy: can't get root vnode for vfsp %x\n",
			vfsp);
		ul->rdevid = 0;
	} else {
		ul->rdevid = (u_long) root_vp->v_rdev;
		/*
		 * VFS_ROOT incremented the reference count of root_vp.
		 */
		VN_RELE(root_vp);
		root_vp = NULL;
	}

	if (unmount_list)
		ul->next = unmount_list;
	unmount_list = ul;

#ifdef AUTODEBUG
	if (autofs_debug)
		printf("get_hierarchical_mounts: try dev=%x, rdev=%x\n",
		vfsp->vfs_dev, ul->rdevid);
#endif
	return (0);
}

/*
 * Given *dap, finds the root of the autofs filesystem, and retriggers
 * the mount of complete hierarchy.
 */
int
force_remount(autonode_t *dap, char *nm, cred_t *cred)
{
	vnode_t	*dvp;
	int error;

#ifdef AUTODEBUG
	if (autofs_debug)
		printf("force_remount: dvp=%x, dap=%x, nm=%s\n", dvp, dap, nm);
#endif
	/*
	 * while not at the root of the autofs...
	 * try parent...
	 */
	while ((antovn(dap)->v_flag & VROOT) == 0) {
		nm = dap->an_name;
		dap = dap->an_parent;
	}

	dvp = antovn(dap);
	VN_HOLD(dvp);
	mutex_enter(&dap->an_lock);
	error = do_mount(dvp, nm, cred);
	mutex_exit(&dap->an_lock);
	VN_RELE(dvp);

#ifdef AUTODEBUG
	if (autofs_debug)
		printf("force_remount: for %s returns %d\n", nm, error);
#endif
	return (error);
}

/*
 * xdr_umntrequest() is an inline way of doing the XDR encoding of
 * the umntrequest data and allows a smaller thread stack. Previously
 * the method used was calling * xdr_pointer(). This caused the threads
 * stack to overrun.
 */
bool_t
xdr_umntrequest(
	register XDR *xdrs,
	umntrequest *objp
)
{
	bool_t more_data;

	ASSERT(xdrs->x_op == XDR_ENCODE);

	for (; objp; objp = objp->next) {
		if (!xdr_u_int(xdrs, &objp->isdirect))
			return (FALSE);
		if (!xdr_u_int(xdrs, &objp->devid))
			return (FALSE);
		if (!xdr_u_long(xdrs, &objp->rdevid))
			return (FALSE);

		if (objp->next != NULL)
			more_data = TRUE;
		else
			more_data = FALSE;
		if (! xdr_bool(xdrs, &more_data))
			return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_mntrequest(
	register XDR *xdrs,
	mntrequest *objp
)
{
	return (xdr_string(xdrs, &objp->name, A_MAXNAME) &&
		xdr_string(xdrs, &objp->map,  A_MAXNAME) &&
		xdr_string(xdrs, &objp->opts, 256)	&&
		xdr_string(xdrs, &objp->path, A_MAXPATH));
}

bool_t
xdr_mntres(
	register XDR *xdrs,
	mntres *objp
)
{
	return (xdr_int(xdrs, &objp->status));
}

bool_t
xdr_umntres(
	register XDR *xdrs,
	umntres *objp
)
{
	return (xdr_int(xdrs, &objp->status));
}

/*
 * Utilities used by both client and server
 * Standard levels:
 * 0) no debugging
 * 1) hard failures
 * 2) soft failures
 * 3) current test software
 * 4) main procedure entry points
 * 5) main procedure exit points
 * 6) utility procedure entry points
 * 7) utility procedure exit points
 * 8) obscure procedure entry points
 * 9) obscure procedure exit points
 * 10) random stuff
 * 11) all <= 1
 * 12) all <= 2
 * 13) all <= 3
 * ...
 */

#ifdef AUTODEBUG
/*VARARGS2*/
auto_dprint(var, level, str, a1, a2, a3, a4, a5, a6, a7, a8, a9)
	int var;
	int level;
	char *str;
	int a1, a2, a3, a4, a5, a6, a7, a8, a9;
{

	if (var == level || (var > 10 && (var - 10) >= level))
		printf(str, a1, a2, a3, a4, a5, a6, a7, a8, a9);
	return (0);
}
#endif
