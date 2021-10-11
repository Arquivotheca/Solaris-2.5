/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)exec.c	1.79	95/03/16 SMI" /* from S5R4 1.33.2.1 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/mman.h>
#include <sys/acct.h>
#include <sys/cpuvar.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/pathname.h>
#include <sys/vm.h>
#include <sys/lock.h>
#include <sys/vtrace.h>
#include <sys/exec.h>
#include <sys/exechdr.h>
#include <sys/kmem.h>
#include <sys/prsystm.h>
#include <sys/modctl.h>
#include <sys/vmparam.h>

#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>

int nullmagic = 0;		/* null magic number */

static int execsetid(struct vnode *, struct vattr *, uid_t *, uid_t *);
static int hold_execsw(struct execsw *);

int auxv_hwcap = 0;	/* auxv AT_SUN_HWCAP value; determined on the fly */
int kauxv_hwcap = 0;	/* analogous kernel version of the same flag */

#ifdef i386
extern void ldt_free(proc_t *pp);
extern faultcode_t forcefault(caddr_t addr, int len);
#endif /* i386 */

/*
 * exec system calls, without and with environments.
 */
int
exec(uap, rvp)
	struct execa *uap;
	rval_t *rvp;
{
	uap->envp = NULL;
	return (exece(uap, rvp));
}

/* ARGSUSED */
int
exece(uap, rvp)
	struct execa *uap;
	rval_t *rvp;
{
	vnode_t *vp = NULL;
	register proc_t *p = ttoproc(curthread);
	klwp_id_t lwp = ttolwp(curthread);
	struct user *up = PTOU(p);
	long execsz;		/* temporary count of exec size */
	int i, error = 0;
	char exec_file[PSCOMSIZ];
	struct pathname pn;
	struct uarg args;

	if (curthread == p->p_aslwptp) {
		/*
		 * The aslwp cannot call exec(). Return error.
		 */
		return (EACCES);
	}
	/*
	 * Leave only the current lwp and force the other lwps
	 * to exit. Since exitlwps() waits until all other lwps are dead, if
	 * the calling process has an aslwp, all pending signals from it will
	 * be transferred to this process before continuing past this call.
	 */
	exitlwps(p, 0);

	ASSERT((p->p_flag & ASLWP) == 0);
	ASSERT(p->p_aslwptp == NULL);
	/*
	 * Delete the dot4 timers.
	 */
	if (p->p_itimer != NULL)
		timer_exit();

	CPU_STAT_ADD_K(cpu_sysinfo.sysexec, 1);

	execsz = btoc(SINCR) + btoc(SSIZE) + btoc(NCARGS-1);

	/*
	 * Lookup path name and remember last component for later.
	 */
	if (error = pn_get(uap->fname, UIO_USERSPACE, &pn))
		return (error);
	if (error = lookuppn(&pn, FOLLOW, NULLVPP, &vp)) {
		pn_free(&pn);
		return (error);
	}
	strncpy(exec_file, pn.pn_path, PSCOMSIZ);
	struct_zero((caddr_t)&args, sizeof (args));
	pn_free(&pn);

	/*
	 * Inform /proc that an exec() has started.
	 */
	prexecstart();

	if (error = gexec(vp, uap, &args, (struct intpdata *)NULL,
	    0, &execsz, (caddr_t)exec_file, p->p_cred))
		goto done;
	/*
	 * Free floating point registers (sun4u only)
	 */
	if (lwp)
		lwp_freeregs(lwp);
	/*
	 * Free device context
	 */
	if (curthread->t_ctx)
		freectx(curthread);

	up->u_execsz = execsz;	/* dependent portion should have checked */

	/*
	 * Remember file name for accounting.
	 */
	up->u_acflag &= ~AFORK;
	bcopy((caddr_t)exec_file, (caddr_t)up->u_comm, PSCOMSIZ);

	/*
	 * Reset stack state to the user stack, clear set of signals
	 * caught on the signal stack, and reset list of signals that
	 * restart system calls; the new program's environment should
	 * not be affected by detritus from the old program. Any pending
	 * signals remain held, so don't clear p_hold and p_sig.
	 */
	mutex_enter(&p->p_lock);
	lwp->lwp_oldcontext = 0;
	sigemptyset(&up->u_signodefer);
	sigemptyset(&up->u_sigonstack);
	sigemptyset(&up->u_sigresethand);
	lwp->lwp_sigaltstack.ss_sp = 0;
	lwp->lwp_sigaltstack.ss_size = 0;
	lwp->lwp_sigaltstack.ss_flags = SS_DISABLE;


	/*
	 * If the action was to catch the signal, then the action
	 * must be reset to SIG_DFL.
	 */
	for (i = 1; i < NSIG; i++) {
		if (up->u_signal[i - 1] != SIG_DFL &&
		    up->u_signal[i - 1] != SIG_IGN) {
			up->u_signal[i - 1] = SIG_DFL;
			sigemptyset(&up->u_sigmask[i - 1]);
			if (sigismember(&ignoredefault, i)) {
				sigdelq(p, NULL, i);
				sigdelq(p, p->p_tlist, i);
			}
		}
	}
	sigorset(&p->p_ignore, &ignoredefault);
	sigdiffset(&p->p_siginfo, &ignoredefault);
	sigdiffset(&p->p_sig, &ignoredefault);
	sigdiffset(&p->p_tlist->t_sig, &ignoredefault);
	p->p_flag &= ~(SNOWAIT|SJCTL|SWAITSIG);
	p->p_flag |= SEXECED;

	/*
	 * Reset lwp id and lpw count to default.
	 * This is a single-threaded process now.
	 */
	curthread->t_tid = 1;
	p->p_lwptotal = 1;
	p->p_lwpblocked = -1;
	/*
	 * Delete the dot4 signal queues.
	 */
	if (p->p_sigqhdr != NULL)
		sigqfree(p);
	mutex_exit(&p->p_lock);

#ifdef i386
	/* If the process uses a private LDT then change it to default */
	if (p->p_ldt)
		ldt_free(p);
#endif

	/*
	 * Close all close-on-exec files.  Don't worry about locking u_lock
	 * when looking at u_nofiles because there should only be one lwp
	 * at this point.
	 */
	close_exec(p);
#ifdef TRACE
	trace_process_name((u_long) (p->p_pid), u.u_psargs);
#endif	/* TRACE */
	TRACE_3(TR_FAC_PROC, TR_PROC_EXEC, "proc_exec:pid %d as %x name %s",
		p->p_pid, p->p_as, up->u_psargs);
	setregs();
done:
	VN_RELE(vp);
	/*
	 * Inform /proc that the exec() has finished.
	 */
	prexecend();
	return (error);
}


/*
 * Perform generic exec duties and switchout to object-file specific
 * handler.
 */
int
gexec(vp, uap, args, idatap, level, execsz, exec_file, cred)
	struct vnode *vp;
	struct execa *uap;
	struct uarg *args;
	struct intpdata *idatap;
	int level;
	long *execsz;
	caddr_t exec_file;
	struct cred *cred;
{
	register proc_t *pp = ttoproc(curthread);
	register struct execsw *eswp;
	int error = 0;
	int resid;
	uid_t uid, gid;
	struct vattr vattr;
	char magbuf[4], tbuf[2];
	short magic, tmagic;
	int setid;
	struct cred *newcred = NULL;

	if ((error = execpermissions(vp, &vattr, args)) != 0)
		goto bad;

	/* need to open vnode for stateful file systems like rfs */
	if ((error = VOP_OPEN(&vp, FREAD, CRED())) != 0)
		goto bad;

	/*
	 * Note: to support binary compatibility with SunOS a.out
	 * executables, we read in the first four bytes, as the
	 * magic number is in bytes 2-3.
	 */
	if (error = vn_rdwr(UIO_READ, vp, magbuf, sizeof (magbuf),
	    (off_t)0, UIO_SYSSPACE, 0, (long)0, CRED(), &resid))
		goto bad;
	if (resid != 0)
		goto bad;

	magic = (short)getexmag(magbuf);

	/*
	 * If this is a SunOS a.out, fix up the magic number.
	 */
	tbuf[0] = magbuf[2];
	tbuf[1] = magbuf[3];
	tmagic = (short)getexmag(tbuf);
	if (tmagic == NMAGIC || tmagic == ZMAGIC || tmagic == OMAGIC)
		magic = tmagic;

	if ((eswp = findexectype(magic)) == NULL)
		goto bad;

	if (level == 0 && execsetid(vp, &vattr, &uid, &gid)) {
		/*
		 * if the suid/euid are not suser, check if
		 * cred will gain any new uid/gid from exec;
		 * if new id's, set a bit in p_flag for core()
		 */
		if (cred->cr_suid && cred->cr_uid) {
			if ((((vattr.va_mode & VSUID) &&
			    uid != cred->cr_suid && uid != cred->cr_uid)) ||
			    ((vattr.va_mode & VSGID) &&
			    gid != cred->cr_sgid && !groupmember(gid, cred))) {
				mutex_enter(&pp->p_lock);
				pp->p_flag |= NOCD;
				mutex_exit(&pp->p_lock);
			}
		}

		newcred = crdup(cred);
		newcred->cr_uid = uid;
		newcred->cr_gid = gid;
		newcred->cr_suid = uid;
		newcred->cr_sgid = gid;
		cred = newcred;
	}

	/*
	 * execsetid() told us whether or not we had to change the
	 * credentials of the process.  It did not tell us whether
	 * the executable is marked setid.  We determine that here.
	 */
	setid = (vp->v_vfsp->vfs_flag & VFS_NOSUID) == 0 &&
		(vattr.va_mode & (VSUID|VSGID)) != 0;

	error = (*eswp->exec_func)(vp, uap, args, idatap, level,
	    execsz, setid, exec_file, cred);
	rw_exit(eswp->exec_lock);
	if (error != 0) {
		if (newcred != NULL)
			crfree(newcred);
		goto bad;
	}

	if (level == 0) {
		if (newcred != NULL) {
			/*
			 * Free the old credentials, and set the new ones.
			 * Do this for both the process and the (single) thread.
			 */
			crfree(pp->p_cred);
			pp->p_cred = cred;	/* cred already held for proc */
			crhold(cred);		/* hold new cred for thread */
			crfree(curthread->t_cred);
			curthread->t_cred = cred;
		}
		if (setid && (pp->p_flag & STRC) == 0) {
			/*
			 * If process is traced via /proc, arrange to
			 * invalidate the associated /proc vnode.
			 */
			if (pp->p_plist || (pp->p_flag & SPROCTR))
				args->traceinval = 1;
		}
		if (pp->p_flag & STRC)
			psignal(pp, SIGTRAP);
		if (args->traceinval)
			prinvalidate(&pp->p_user);
	}
	return (0);
bad:
	if (error == 0)
		error = ENOEXEC;
	return (error);
}

extern char *execswnames[];

struct execsw *
allocate_execsw(char *name, short magic)
{
	register int i;
	register char *ename;
	register short *magicp;

	mutex_enter(&execsw_lock);
	for (i = 0; i < nexectype; i++) {
		if (execswnames[i] == NULL) {
			ename = kmem_alloc(strlen(name) + 1, KM_SLEEP);
			strcpy(ename, name);
			execswnames[i] = ename;
			/*
			 * Set the magic number last so that we
			 * don't need to hold the execsw_lock in
			 * findexectype().
			 */
			magicp = (short *)kmem_alloc(sizeof (short), KM_SLEEP);
			*magicp = magic;
			execsw[i].exec_magic = magicp;
			mutex_exit(&execsw_lock);
			return (&execsw[i]);
		}
	}
	mutex_exit(&execsw_lock);
	return (NULL);
}

struct execsw *
findexecsw(short magic)
{
	register struct execsw *eswp;

	for (eswp = execsw; eswp < &execsw[nexectype]; eswp++) {
		if (magic && magic == *eswp->exec_magic)
			return (eswp);
	}
	return (NULL);
}

/*
 * Find the execsw[] index for a given magic number.  If no execsw[] entry
 * is found, try to autoload a module for this magic number.
 */
struct execsw *
findexectype(short magic)
{
	register struct execsw *eswp;

	for (eswp = execsw; eswp < &execsw[nexectype]; eswp++) {
		if (magic && magic == *eswp->exec_magic) {
			if (hold_execsw(eswp) != 0)
				return (NULL);
			return (eswp);
		}
	}
	return (NULL);	/* couldn't find the type */
}

static int
hold_execsw(struct execsw *eswp)
{
	register char *name;

	rw_enter(eswp->exec_lock, RW_READER);
	while (!LOADED_EXEC(eswp)) {
		rw_exit(eswp->exec_lock);
		name = execswnames[eswp-execsw];
		ASSERT(name);
		if (modload("exec", name) == -1)
			return (-1);
		rw_enter(eswp->exec_lock, RW_READER);
	}
	return (0);
}

static int
execsetid(vp, vattrp, uidp, gidp)
	struct vnode *vp;
	struct vattr *vattrp;
	uid_t *uidp;
	uid_t *gidp;
{
	register proc_t *pp = ttoproc(curthread);
	uid_t uid, gid;

	/*
	 * Remember credentials.
	 */
	uid = pp->p_cred->cr_uid;
	gid = pp->p_cred->cr_gid;

	if ((vp->v_vfsp->vfs_flag & VFS_NOSUID) == 0) {
		if (vattrp->va_mode & VSUID)
			uid = vattrp->va_uid;
		if (vattrp->va_mode & VSGID)
			gid = vattrp->va_gid;
	}

	/*
	 * Set setuid/setgid protections if no ptrace() compatibility.
	 * For the super-user, honor setuid/setgid even in
	 * the presence of ptrace() compatibility.
	 */
	if (((pp->p_flag & STRC) == 0 || pp->p_cred->cr_uid == 0) &&
	    (pp->p_cred->cr_uid != uid ||
	    pp->p_cred->cr_gid != gid ||
	    pp->p_cred->cr_suid != uid ||
	    pp->p_cred->cr_sgid != gid)) {
		*uidp = uid;
		*gidp = gid;
		return (1);
	}
	return (0);
}

int
execpermissions(vp, vattrp, args)
	struct vnode *vp;
	struct vattr *vattrp;
	struct uarg *args;
{
	int error;
	register proc_t *p = ttoproc(curthread);

	vattrp->va_mask = AT_MODE|AT_UID|AT_GID|AT_SIZE;
	if (error = VOP_GETATTR(vp, vattrp, ATTR_EXEC, p->p_cred))
		return (error);
	/*
	 * Check the access mode.
	 */
	if ((error = VOP_ACCESS(vp, VEXEC, 0, p->p_cred)) != 0 ||
	    vp->v_type != VREG || (vattrp->va_mode &
	    (VEXEC|(VEXEC>>3)|(VEXEC>>6))) == 0) {
		if (error == 0)
			error = EACCES;
		return (error);
	}

	if ((p->p_plist || (p->p_flag & (STRC|SPROCTR))) &&
	    (error = VOP_ACCESS(vp, VREAD, 0, p->p_cred))) {
		/*
		 * If process is under ptrace(2) compatibility,
		 * fail the exec(2).
		 */
		if (p->p_flag & STRC)
			goto bad;
		/*
		 * Process is traced via /proc.
		 * Arrange to invalidate the /proc vnode.
		 */
		args->traceinval = 1;
	}
	return (0);
bad:
	if (error == 0)
		error = ENOEXEC;
	return (error);
}

/*
 * Map a section of an executable file into the user's
 * address space.
 */
int
execmap(vp, addr, len, zfodlen, offset, prot, page)
	struct vnode *vp;
	caddr_t addr;
	size_t len, zfodlen;
	off_t  offset;
	int prot;
	int page;
{
	int error = 0;
	off_t oldoffset;
	caddr_t zfodbase, oldaddr;
	size_t  end, oldlen;
	int	zfoddiff;
	label_t ljb;
	register proc_t *p = ttoproc(curthread);

	oldaddr = addr;
	addr = (caddr_t)((long)addr & PAGEMASK);
	if (len) {
		oldlen = len;
		len += ((size_t)oldaddr - (size_t)addr);
		oldoffset = offset;
		offset = (off_t)((long)offset & PAGEMASK);
		if (page) {
			int npages, preread, prefltmem, availm;

			if (error = VOP_MAP(vp, (offset_t)offset,
			    p->p_as, &addr, len, prot, PROT_ALL,
			    MAP_PRIVATE | MAP_FIXED, CRED()))
				goto bad;

			/*
			 * If the segment can fit, then we prefault
			 * the entire segment in.  This is based on the
			 * model that says the best working set of a
			 * small program is all of its pages.
			 */
			npages = btopr(len);
			prefltmem = freemem - desfree;
			preread =
			    (npages < prefltmem && len < PGTHRESH) ? 1 : 0;

			/*
			 * If we aren't prefaulting the segment,
			 * increment "deficit", if necessary to ensure
			 * that pages will become available when this
			 * process starts executing.
			 */
			availm = freemem - lotsfree;
			if (preread == 0 && npages > availm &&
			    deficit < lotsfree) {
				deficit += MIN(npages - availm,
				    lotsfree - deficit);
			}

			if (preread) {
				TRACE_2(TR_FAC_PROC, TR_EXECMAP_PREREAD,
					"execmap preread:freemem %d size %d",
					freemem, len);
				(void) as_fault(p->p_as->a_hat, p->p_as,
				    (caddr_t)addr, len, F_INVAL, S_READ);
			}
#ifdef TRACE
			else {
				TRACE_2(TR_FAC_PROC, TR_EXECMAP_NO_PREREAD,
					"execmap no preread:freemem %d size %d",
					freemem, len);
			}
#endif
		} else {
			if (error = as_map(p->p_as, addr, len,
			    segvn_create, zfod_argsp))
				goto bad;
			/*
			 * Read in the segment in one big chunk.
			 */
			if (error = vn_rdwr(UIO_READ, vp, (caddr_t)oldaddr,
			    oldlen, oldoffset, UIO_USERSPACE, 0,
			    (long)0, CRED(), (int *)0))
				goto bad;
			/*
			 * Now set protections.
			 */
			if (prot != PROT_ALL) {
				(void) as_setprot(p->p_as, (caddr_t)addr,
				    len, prot);
			}
		}
	}

	if (zfodlen) {
		end = (size_t)addr + len;
		zfodbase = (caddr_t)roundup(end, PAGESIZE);
		zfoddiff = (int)zfodbase - end;
		if (zfoddiff > 0) {
#ifdef i386
			(void) forcefault((caddr_t)end, zfoddiff);
#endif
			if (on_fault(&ljb))
				goto bad;
			(void) uzero((caddr_t)end, zfoddiff);
			no_fault();
		}
		if (zfodlen > zfoddiff) {
			zfodlen -= zfoddiff;
			if (error = as_map(p->p_as, (caddr_t)zfodbase,
			    zfodlen, segvn_create, zfod_argsp))
				goto bad;
			if (prot != PROT_ALL) {
				(void) as_setprot(p->p_as, (caddr_t)zfodbase,
				    zfodlen, prot);
			}
		}
	}
	return (0);
bad:
	return (error);
}

void
setexecenv(ep)
	struct execenv *ep;
{
	proc_t *p = ttoproc(curthread);
	klwp_id_t lwp = ttolwp(curthread);
	struct vnode *vp;

	p->p_brkbase = ep->ex_brkbase;
	p->p_brksize = ep->ex_brksize;
	if (p->p_exec)
		VN_RELE(p->p_exec);	/* out with the old */
	vp = p->p_exec = ep->ex_vp;
	if (vp)
		VN_HOLD(vp);		/* in with the new */

	lwp->lwp_sigaltstack.ss_sp = 0;
	lwp->lwp_sigaltstack.ss_size = 0;
	lwp->lwp_sigaltstack.ss_flags = SS_DISABLE;

	p->p_user.u_execid = (int)ep->ex_magic;
}

int
execopen(vpp, fdp)
	struct vnode **vpp;
	int *fdp;
{
	struct vnode *vp = *vpp;
	file_t *fp;
	int error = 0;
	int filemode = FREAD;

	VN_HOLD(vp);		/* open reference */
	if (error = falloc((struct vnode *)NULL, filemode, &fp, fdp)) {
		VN_RELE(vp);
		*fdp = -1;	/* just in case falloc changed value */
		return (error);
	}
	if (error = VOP_OPEN(&vp, filemode, CRED())) {
		VN_RELE(vp);
		setf(*fdp, NULLFP);
		unfalloc(fp);
		*fdp = -1;
		return (error);
	}
	*vpp = vp;		/* vnode should not have changed */
	fp->f_vnode = vp;
	mutex_exit(&fp->f_tlock);
	setf(*fdp, fp);
	return (0);
}

int
execclose(fd)
	int fd;
{
	file_t *fp;

	if (fp = getandset(fd))
		return (closef(fp));
	else
		return (EBADF);
}

/*
 * noexec stub function.
 */

/* ARGSUSED */
int
noexec(vp, uap, args, idatap, level, execsz, setid, exec_file, cred)
	struct vnode *vp;
	struct execa *uap;
	struct uarg *args;
	struct intpdata *idatap;
	int level;
	long *execsz;
	int setid;
	caddr_t exec_file;
	struct cred *cred;
{
	cmn_err(CE_WARN, "missing exec capability for %s\n", uap->fname);
	return (ENOEXEC);
}
