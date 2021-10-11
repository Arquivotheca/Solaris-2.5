/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strcalls.c	1.18	94/08/12 SMI"	/* SVr4 1.20	*/

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/fs/fifonode.h>
#include <sys/debug.h>

/*
 * STREAMS system calls.
 */

struct msgnp {		/* non-priority version retained for compatibility */
	int fdes;
	struct strbuf *ctl;
	struct strbuf *data;
	int flags;
};

struct msgp {		/* priority version */
	int fdes;
	struct strbuf *ctl;
	struct strbuf *data;
	int pri;
	int flags;
};

int getmsg(struct msgnp *, rval_t *);
int putmsg(struct msgnp *, rval_t *);
int getpmsg(struct msgp *, rval_t *);
int putpmsg(struct msgp *, rval_t *);

static int msgio(struct msgp *, rval_t *, int, unsigned char *, int *);

int
getmsg(
	struct msgnp *uap,
	rval_t *rvp
)
{
	struct msgp ua;
	int error;
	int localflags;
	int realflags = 0;
	unsigned char pri = 0;

	/*
	 * Convert between old flags (localflags) and new flags (realflags).
	 */
	if (copyin((caddr_t)uap->flags, (caddr_t)&localflags, sizeof (int)))
		return (EFAULT);
	switch (localflags) {
	case 0:
		realflags = MSG_ANY;
		break;

	case RS_HIPRI:
		realflags = MSG_HIPRI;
		break;

	default:
		return (EINVAL);
	}

	ua.fdes = uap->fdes;
	ua.ctl = uap->ctl;
	ua.data = uap->data;
	if ((error = msgio(&ua, rvp, FREAD, &pri, &realflags)) == 0) {
		/*
		 * massage realflags based on localflags.
		 */
		if (realflags == MSG_HIPRI)
			localflags = RS_HIPRI;
		else
			localflags = 0;
		if (copyout((caddr_t)&localflags, (caddr_t)uap->flags,
		    sizeof (int)))
			error = EFAULT;
	}
	return (error);
}

int
putmsg(
	struct msgnp *uap,
	rval_t *rvp
)
{
	unsigned char pri = 0;
	int flags;

	switch (uap->flags) {
	case RS_HIPRI:
		flags = MSG_HIPRI;
		break;

	case 0:
		flags = MSG_BAND;
		break;

	default:
		return (EINVAL);
	}
	return (msgio((struct msgp *)uap, rvp, FWRITE, &pri, &flags));
}

int
getpmsg(
	struct msgp *uap,
	rval_t *rvp
)
{
	int error;
	int flags;
	int intpri;
	unsigned char pri;

	if (copyin((caddr_t)uap->flags, (caddr_t)&flags, sizeof (int)))
		return (EFAULT);
	if (copyin((caddr_t)uap->pri, (caddr_t)&intpri, sizeof (int)))
		return (EFAULT);
	if ((intpri > 255) || (intpri < 0))
		return (EINVAL);
	pri = (unsigned char)intpri;
	if ((error = msgio(uap, rvp, FREAD, &pri, &flags)) == 0) {
		if (copyout((caddr_t)&flags, (caddr_t)uap->flags, sizeof (int)))
			return (EFAULT);
		intpri = (int)pri;
		if (copyout((caddr_t)&intpri, (caddr_t)uap->pri, sizeof (int)))
			error = EFAULT;
	}
	return (error);
}

int
putpmsg(
	struct msgp *uap,
	rval_t *rvp
)
{
	unsigned char pri;

	if ((uap->pri > 255) || (uap->pri < 0))
		return (EINVAL);
	pri = (unsigned char)uap->pri;
	return (msgio(uap, rvp, FWRITE, &pri, &uap->flags));
}

/*
 * Common code for getmsg and putmsg calls: check permissions,
 * copy in args, do preliminary setup, and switch to
 * appropriate stream routine.
 */
static int
msgio(
	register struct msgp *uap,
	rval_t *rvp,
	register int mode,
	unsigned char *prip,
	int *flagsp
)
{
	file_t *fp;
	register vnode_t *vp;
	struct strbuf msgctl, msgdata;
	register int error;
	int flag;
	klwp_t *lwp = ttolwp(curthread);

	if ((fp = GETF(uap->fdes)) == NULL)
		return (EBADF);
	if ((fp->f_flag & mode) == 0) {
		RELEASEF(uap->fdes);
		return (EBADF);
	}
	vp = fp->f_vnode;
	if (vp->v_type == VFIFO) {
		if (vp->v_stream) {
			/*
			 * must use sd_vnode, could be named pipe
			 */
			(void) fifo_vfastoff(vp->v_stream->sd_vnode);
		} else {
			RELEASEF(uap->fdes);
			return (ENOSTR);
		}
	} else if (vp->v_type != VCHR || vp->v_stream == NULL) {
		RELEASEF(uap->fdes);
		return (ENOSTR);
	}
	if (uap->ctl && copyin((caddr_t)uap->ctl, (caddr_t)&msgctl,
	    sizeof (struct strbuf))) {
		RELEASEF(uap->fdes);
		return (EFAULT);
	}
	if (uap->data && copyin((caddr_t)uap->data, (caddr_t)&msgdata,
	    sizeof (struct strbuf))) {
		RELEASEF(uap->fdes);
		return (EFAULT);
	}
	if (mode == FREAD) {
		if (!uap->ctl)
			msgctl.maxlen = -1;
		if (!uap->data)
			msgdata.maxlen = -1;
		flag = fp->f_flag;
		if (error = strgetmsg(vp, &msgctl, &msgdata, prip,
		    flagsp, flag, rvp)) {
			RELEASEF(uap->fdes);
			return (error);
		}
		if (lwp != NULL)
			lwp->lwp_ru.msgrcv++;
		if ((uap->ctl && copyout((caddr_t)&msgctl, (caddr_t)uap->ctl,
		    sizeof (struct strbuf))) || (uap->data &&
		    copyout((caddr_t)&msgdata, (caddr_t)uap->data,
		    sizeof (struct strbuf)))) {
			RELEASEF(uap->fdes);
			return (EFAULT);
		}
		RELEASEF(uap->fdes);
		return (0);
	}
	/*
	 * FWRITE case
	 */
	if (!uap->ctl)
		msgctl.len = -1;
	if (!uap->data)
		msgdata.len = -1;
	flag = fp->f_flag;
	error =  strputmsg(vp, &msgctl, &msgdata, *prip, *flagsp, flag);
	RELEASEF(uap->fdes);
	if (error == 0 && lwp != NULL)
		lwp->lwp_ru.msgsnd++;
	return (error);
}
