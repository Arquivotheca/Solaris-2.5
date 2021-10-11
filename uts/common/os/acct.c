/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)acct.c	1.25	95/08/27 SMI" /* from SVR4.0 1.18 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/acct.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/session.h>
#include <sys/modctl.h>
#include <sys/syscall.h>

extern struct acct	acctbuf;
extern struct vnode	*acctvp;
extern kmutex_t		aclock;

static struct sysent acctsysent = {
	1,
	0,
	sysacct
};

static struct modlsys modlsys = {
	&mod_syscallops, "accounting", &acctsysent
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlsys, NULL
};

int
_init()
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * acct() is a "weak stub" routine called from exit().
 * Once this module has been loaded, we refuse to allow
 * it to unload - otherwise accounting would quietly
 * cease.  See 1211661.  It's possible to make this module
 * unloadable but it's substantially safer not to bother.
 */
int
_fini(void)
{
	return (EBUSY);
}

/*
 * Perform process accounting functions.
 */

struct accta {
	char	*fname;
};

/*ARGSUSED2*/
int
sysacct(struct accta *uap, rval_t *rvp)
{
	struct vnode *vp;
	int error = 0;

	if (!suser(CRED()))
		return (EPERM);

	if (uap->fname == NULL) {
		/*
		 * Close the file and stop accounting.
		 */
		mutex_enter(&aclock);
		vp = acctvp;
		acctvp = NULL;
		mutex_exit(&aclock);
		if (vp) {
			error = VOP_CLOSE(vp, FWRITE, 1, (offset_t)0, CRED());
			VN_RELE(vp);
		}
		return (error);
	}

	/*
	 * Either (a) open a new file and begin accounting -or- (b)
	 * switch accounting from an old to a new file.
	 *
	 * (Open the file without holding aclock in case it
	 * sleeps (holding the lock prevents process exit).)
	 */
	if ((error = vn_open(uap->fname, UIO_USERSPACE, FWRITE,
	    0, &vp, (enum create)0)) != 0) {
		/* SVID  compliance */
		if (error == EISDIR)
			error = EACCES;
		return (error);
	}

	if (vp->v_type != VREG)
		error = EACCES;
	else {
		mutex_enter(&aclock);
		if (acctvp && VN_CMP(acctvp, vp))
			error = EBUSY;
		else if (acctvp) {
			vnode_t *oldvp;

			/*
			 * close old acctvp, and point acct()
			 * at new file by swapping vp and acctvp
			 */
			oldvp = acctvp;
			acctvp = vp;
			vp = oldvp;
		} else {
			/*
			 * no existing file, start accounting ..
			 */
			acctvp = vp;
			vp = NULL;
		}
		mutex_exit(&aclock);
	}

	if (vp) {
		(void) VOP_CLOSE(vp, FWRITE, 1, (offset_t)0, CRED());
		VN_RELE(vp);
	}
	return (error);
}

/*
 * Produce a pseudo-floating point representation
 * with 3 bits base-8 exponent, 13 bits fraction.
 */

static int
compress(time_t t)
{
	register exp = 0, round = 0;

	while (t >= 8192) {
		exp++;
		round = t&04;
		t >>= 3;
	}
	if (round) {
		t++;
		if (t >= 8192) {
			t >>= 3;
			exp++;
		}
	}
	return ((exp << 13) + t);
}

/*
 * On exit, write a record on the accounting file.
 */
void
acct(char st)
{
	register struct vnode *vp;
	register struct cred *cr;
	register struct proc *p;
	struct vattr va;
	int resid = 0;
	int error;

	mutex_enter(&aclock);
	if ((vp = acctvp) == NULL) {
		mutex_exit(&aclock);
		return;
	}

	/*
	 * This only gets called from exit after all lwp's have exited so no
	 * cred locking is needed.
	 */
	p = curproc;
	bcopy(u.u_comm, acctbuf.ac_comm, sizeof (acctbuf.ac_comm));
	acctbuf.ac_btime = u.u_start;
	acctbuf.ac_utime = compress(p->p_utime);
	acctbuf.ac_stime = compress(p->p_stime);
	acctbuf.ac_etime = compress(lbolt - u.u_ticks);
	acctbuf.ac_mem = compress(u.u_mem);
	acctbuf.ac_io = compress(p->p_ru.ioch);
	acctbuf.ac_rw = compress(p->p_ru.inblock + p->p_ru.oublock);
	cr = CRED();
	acctbuf.ac_uid = cr->cr_ruid;
	acctbuf.ac_gid = cr->cr_rgid;
	acctbuf.ac_tty = cttydev(p);
	acctbuf.ac_stat = st;
	acctbuf.ac_flag = (u.u_acflag | AEXPND);
	/*
	 * Save the size. If the write fails, reset the size to avoid
	 * corrupted acct files.
	 */
	va.va_mask = AT_SIZE;
	if (VOP_GETATTR(vp, &va, 0, kcred) == 0) {
		error = vn_rdwr(UIO_WRITE, vp, (caddr_t) &acctbuf,
		    sizeof (acctbuf), 0, UIO_SYSSPACE, FAPPEND,
		    RLIM_INFINITY, kcred, &resid);
		if (error || resid) {
			(void) VOP_SETATTR(vp, &va, 0, kcred);
		}
	}
	mutex_exit(&aclock);
}
