/*
 * Copyright (c) 1992-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)inst_sync.c	1.10	94/06/10 SMI"

/*
 * Syscall to write out the instance number data structures to
 * stable storage.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/t_lock.h>
#include <sys/modctl.h>
#include <sys/systm.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/cred.h>
#include <sys/file.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/dditypes.h>
#include <sys/instance.h>
#include <sys/instance.h>
#include <sys/debug.h>

/*
 * Userland sees:
 *
 *	int inst_sync(pathname, flags);
 *
 * Returns zero if instance number information was successfully
 * written to 'pathname', -1 plus error code in errno otherwise.
 *
 * POC notes:
 *
 * -	This could be done as a case of the modctl(2) system call
 *	though the ability to have it load and unload would disappear.
 *
 * -	Currently, flags are not interpreted.
 *
 * -	Maybe we should pass through two filenames - one to create,
 *	and the other as the 'final' target i.e. do the rename of
 *	/etc/instance.new -> /etc/instance in the kernel.
 */

struct instance_synca {
	char	*pathname;	/* where to write kernel state */
	int	flags;		/* to change detailed semantics */
};

static int in_sync_sys(struct instance_synca *uap, rval_t *rvp);

static struct sysent in_sync_sysent = {
	2,		/* number of arguments */
	0,		/* no setjmp, async or explicit i/o */
	in_sync_sys,	/* the handler */
	(krwlock_t *)0	/* rw lock allocated/used by framework */
};

static struct modlsys modlsys = {
	&mod_syscallops, "instance binding syscall", &in_sync_sysent
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlsys, NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

static int in_write_instance(struct vnode *vp);

/*ARGSUSED1*/
static int
in_sync_sys(struct instance_synca *uap, rval_t *rvp)
{
	struct vnode *vp;
	register int error;

	/*
	 * We must be root to do this, since we lock critical data
	 * structures whilst we're doing it ..
	 */
	if (!suser(CRED())) {
		return (EPERM);
		/* NOTREACHED */
	}

	/*
	 * Only one process is allowed to get the state of the instance
	 * number assignments on the system at any given time.
	 */
	mutex_enter(&e_ddi_inst_state.in_serial);
	while (e_ddi_inst_state.in_busy)
		cv_wait(&e_ddi_inst_state.in_serial_cv,
		    &e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 1;
	mutex_exit(&e_ddi_inst_state.in_serial);

	/*
	 * Create an instance file for writing, giving it a mode that
	 * will only permit reading.  Note that we refuse to overwrite
	 * an existing file.
	 */
	if ((error = vn_open(uap->pathname, UIO_USERSPACE,
	    FCREAT, 0444, &vp, CRCREAT)) != 0) {
		if (error == EISDIR)
			error = EACCES;	/* SVID compliance? */
		goto end;
		/*NOTREACHED*/
	}

	/*
	 * So far so good.  We're singly threaded, the vnode is beckoning
	 * so let's get on with it.  Any error, and we just give up and
	 * hand the first error we get back to userland.
	 */
	error = in_write_instance(vp);

	/*
	 * If there was any sort of error, we deliberately go and
	 * remove the file we just created so that any attempts to
	 * use it will quickly fail.
	 */
	if (error)
		(void) vn_remove(uap->pathname, UIO_USERSPACE, RMFILE);
end:
	mutex_enter(&e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 0;
	cv_broadcast(&e_ddi_inst_state.in_serial_cv);
	mutex_exit(&e_ddi_inst_state.in_serial);

	return (error);
}

/*
 * At the risk of reinventing stdio ..
 */
#define	FBUFSIZE	512

typedef struct _File {
	char	*ptr;
	int	count;
	char	buf[FBUFSIZE];
	vnode_t	*vp;
	offset_t voffset;
} File;

static int
in_write(struct vnode *vp, offset_t *vo, caddr_t buf, int count)
{
	register int error;
	int resid;
	register long rlimit = *vo + count + 1;

	error = vn_rdwr(UIO_WRITE, vp, buf, count, *vo,
	    UIO_SYSSPACE, 0, rlimit, CRED(), &resid);

	*vo += (offset_t)(count - resid);

	return (error);
}

static File *
in_fvpopen(struct vnode *vp)
{
	File *fp;

	fp = kmem_zalloc(sizeof (File), KM_SLEEP);
	fp->vp = vp;
	fp->ptr = fp->buf;

	return (fp);
}

static int
in_fclose(File *fp)
{
	register int error;

	error = VOP_CLOSE(fp->vp, FCREAT, 1, (offset_t)0, CRED());
	VN_RELE(fp->vp);
	kmem_free(fp, sizeof (File));
	return (error);
}

static int
in_fflush(File *fp)
{
	register int error = 0;

	if (fp->count)
		error = in_write(fp->vp, &fp->voffset, fp->buf, fp->count);
	if (error == 0)
		error = VOP_FSYNC(fp->vp, FSYNC,  CRED());
	return (error);
}

static int
in_fputs(File *fp, char *buf)
{
	register int error = 0;

	while (*buf) {
		*fp->ptr++ = *buf++;
		if (++fp->count == FBUFSIZE) {
			error = in_write(fp->vp, &fp->voffset, fp->buf,
			    fp->count);
			if (error)
				break;
			fp->count = 0;
			fp->ptr = fp->buf;
		}
	}

	return (error);
}

/*
 * External linkage
 */
static File *in_fp;

static char linebuffer[MAXPATHLEN + 1 + 1 + 1 + 1 + 10 + 1];

/*
 * XXX	Maybe we should just write 'in_fprintf' instead ..
 */
static int
in_walktree(in_node_t *np, register char *this)
{
	register char *next;
	register int error = 0;

	for (error = 0; np; np = np->in_sibling) {

		if (np->in_addr[0] == '\0')
			sprintf(this, "/%s", np->in_name);
		else
			sprintf(this, "/%s@%s", np->in_name, np->in_addr);
		next = this + strlen(this);
		sprintf(next, "\" %d\n", np->in_instance);

		if (error = in_fputs(in_fp, linebuffer))
			break;

		if (np->in_child)
			if (error = in_walktree(np->in_child, next))
				break;
	}
	return (error);
}


/*
 * Walk the instance tree, writing out what we find.
 *
 * There's some fairly nasty sharing of buffers in this
 * bit of code, so be careful out there when you're
 * rewriting it ..
 */
static int
in_write_instance(struct vnode *vp)
{
	register int error;
	register char *cp;

	in_fp = in_fvpopen(vp);

	/*
	 * Place a bossy comment at the beginning of the file.
	 */
	error = in_fputs(in_fp,
	    "#\n#\tCaution! This file contains critical kernel state\n#\n");

	if (error == 0) {
		ASSERT(e_ddi_inst_state.in_busy);

		cp = linebuffer;
		*cp++ = '\"';
		error = in_walktree(e_ddi_inst_state.in_root->in_child, cp);
	}

	if (error == 0) {
		if ((error = in_fflush(in_fp)) == 0)
			error = in_fclose(in_fp);
	} else
		(void) in_fclose(in_fp);

	return (error);
}
