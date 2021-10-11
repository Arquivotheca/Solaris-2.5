/*
 * Copyright (C) 1990, Sun Microsystems, Inc.
 */

#pragma ident	"@(#)l_strplumb.c	1.19	94/03/31 SMI"

#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/user.h>
#include	<sys/vfs.h>
#include	<sys/vnode.h>
#include	<sys/file.h>
#include	<sys/stream.h>
#include	<sys/stropts.h>
#include	<sys/strsubr.h>
#include	<sys/dlpi.h>
#include	<sys/vnode.h>
#include	<sys/socket.h>
#include	<sys/sockio.h>
#include	<sys/cmn_err.h>
#include	<net/if.h>
#include	<sys/sad.h>
#include	<sys/kstr.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>

#include	<sys/cred.h>
#include	<sys/sysmacros.h>

#include	<sys/modctl.h>

/*
 * Routines to allow strplumb() legitimate access
 * to the kernel.
 */
int
kstr_open(maj, min, vpp, fd)
	dev_t		maj;
	dev_t		min;
	vnode_t		**vpp;
	int		*fd;
{
	int		error;
	vnode_t		*vp;
	int		fdp;

	vp = makespecvp(makedevice(maj, min), VCHR);

	if (error = fassign(&vp, FREAD|FWRITE, &fdp)) {
		return (error);
	}

	if (vpp)
		*vpp = vp;
	if (fd)
		*fd = fdp;
	return (0);
}

int
kstr_plink(vp, fd, mux_id)
	vnode_t	*vp;
	int	fd;
	int	*mux_id;
{
	int	id;
	int	error;

	if (error = strioctl(vp, I_PLINK, fd, 0, K_TO_K, CRED(), &id))
		return (error);
	if (mux_id)
		*mux_id = id;
	return (0);
}

int
kstr_unplink(vp, mux_id)
	vnode_t	*vp;
	int	mux_id;
{
	int	rval;

	return (strioctl(vp, I_PUNLINK, mux_id, 0, K_TO_K, CRED(), &rval));
}

int
kstr_push(vp, mod)
	vnode_t	*vp;
	char	*mod;
{
	int	rval;

	return (strioctl(vp, I_PUSH, (int) mod, 0, K_TO_K, CRED(), &rval));
}

int
kstr_pop(vp)
	vnode_t	*vp;
{
	int	rval;

	return (strioctl(vp, I_POP, 0, 0, K_TO_K, CRED(), &rval));
}

int
kstr_close(vp, fd)
	vnode_t	*vp;
	int	fd;
{
	file_t	*fp;

	if (vp == (vnode_t *)NULL && fd == -1)
		return (EINVAL);

	if (fd != -1) {
		if (fp = getandset(fd)) {
			(void) closef(fp);
			return (0);
		} else {
			printf("close_fd: bad fd %d\n", fd);
			return (EINVAL);
		}

	} else
		return (VOP_CLOSE(vp, FREAD|FWRITE, 1, (offset_t)0, CRED()));
}

int
kstr_ioctl(vp, cmd, arg)
	register struct vnode	*vp;
	register int		cmd;
	register int 		arg;
{
	int	rval;

	return (strioctl(vp, cmd, arg, 0, K_TO_K, CRED(), &rval));
}

/*
 * This routine does a cv_broadcast on the condition variable that
 * strwaitq is sleeping on.
 * Note: callers to untimeout() can not hold sd_lock since this
 * routine acquires that lock.
 */
static void
kstr_msg_timeout(stdata_t *stp)
{
	mutex_enter(&stp->sd_lock);
	if (stp->sd_flag & STR4TIME) {
		stp->sd_flag &= ~STR4TIME;
		cv_broadcast(&RD(stp->sd_wrq)->q_wait);
	}
	mutex_exit(&stp->sd_lock);
}

int
kstr_msg(vp, smp, rmp, timeo)
	register struct vnode	*vp;
	register mblk_t		*smp;
	register mblk_t		**rmp;
	register timestruc_t	*timeo;
{
	register struct stdata	*stp;
	register mblk_t		*bp;
	int			retval;
	int			error;
	long			ticks;
	int			tid = 0;

	if (rmp == (mblk_t **)NULL && timeo != NULL &&
			(timeo->tv_sec != 0 || timeo->tv_nsec != 0))
		return (EINVAL);

	if (smp == NULL && rmp == NULL)
		return (EINVAL);

	stp = (stdata_t *)vp->v_stream;

	if (smp != NULL)
		(void) putnext(stp->sd_wrq, smp);

	if (rmp == NULL) {
		return (0);
	}

	if (timeo && timeo->tv_sec == 0 && timeo->tv_nsec == 0) {
		*rmp = getq(RD(stp->sd_wrq));
		return (0);
	}

	/*
	 * Convert from nano seconds to ticks.
	 */
	if (timeo != NULL) {
		ticks = timeo->tv_sec * HZ +
			timeo->tv_nsec * HZ / 1000000000;

	} else
		ticks = 0;
	/*
	 * If there is nothing on the read side queue set up a timeout and
	 * sleep.
	 */
	mutex_enter(&stp->sd_lock);
	while ((bp = getq(RD(stp->sd_wrq))) == (mblk_t *)NULL) {
		if (ticks) {
			stp->sd_flag |= STR4TIME;
			tid = timeout(kstr_msg_timeout, (caddr_t)stp, ticks);
		}
		if ((error = strwaitq(stp, READWAIT, (off_t)0, FREAD|FWRITE,
		    &retval)) != 0 || retval != 0) {
			mutex_exit(&stp->sd_lock);
			if (tid && (stp->sd_flag & STR4TIME))
				(void) untimeout(tid);
			*rmp = NULL;
			return (error);
		}
		if (tid) {
			if (stp->sd_flag & STR4TIME) {
				/* Timer still running */
				mutex_exit(&stp->sd_lock);
				(void) untimeout(tid);
				mutex_enter(&stp->sd_lock);
			} else {
				/* Timer fired */
				mutex_exit(&stp->sd_lock);
				*rmp = NULL;
				return (0);
			}
		}
	}

	if (bp->b_datap->db_type == M_PCPROTO)
		stp->sd_flag &= ~STRPRI;
	mutex_exit(&stp->sd_lock);
	*rmp = bp;
	return (0);
}

int SAD_MAJOR;	/* major number for SAD device. */
/*
 * It is the callers responsibility to make sure that "mods"
 * conforms to what is required. We do not check it here.
 *
 * "maj", "min", and "lastmin" are value-result parameters.
 */
int
kstr_autopush(op, maj, min, lastmin, mods)
	int		op;
	dev_t		*maj;
	dev_t		*min;
	dev_t		*lastmin;
	char		*mods[];   /* null terminated array of charactor ptrs */
{
	struct strapush	push;
	register int	error;
	vnode_t		*vp;
	register int	i;

	SAD_MAJOR = ddi_name_to_major("sad");
	if (op == SET_AUTOPUSH || op == CLR_AUTOPUSH) {
		if (error = kstr_open(SAD_MAJOR, ADMMIN, &vp, (int *)NULL)) {
			printf("kstr_autopush: open failed error %d\n", error);
			return (error);
		}
	} else	{
		if (error = kstr_open(SAD_MAJOR, USRMIN, &vp, (int *)NULL)) {
			printf("kstr_autopush: open failed error %d\n", error);
			return (error);
		}
	}

	switch (op) {
	case GET_AUTOPUSH:
		/* Get autopush information */

		push.sap_major = *maj;
		push.sap_minor = *min;
		if (error = kstr_ioctl(vp, SAD_GAP, (int)&push)) {
			printf("kstr_autopush: ioctl failed, error %d\n",
				error);
			(void) kstr_close(vp, -1);
			return (error);
		}
		switch (push.sap_cmd) {
		case SAP_ONE:
			*maj = push.sap_major;
			*min = push.sap_minor;
			*lastmin = 0;
			break;

		case SAP_RANGE:
			*maj = push.sap_major;
			*min = push.sap_minor;
			*lastmin = push.sap_lastminor;
			break;

		case SAP_ALL:
			*maj = push.sap_major;
			*min = (dev_t) -1;
			break;
		}
		if (push.sap_npush > 1) {
			for (i = 0; i < push.sap_npush &&
					mods[i] != (char *)NULL; i++)
				strcpy(mods[i], push.sap_list[i]);
			mods[i] = (char *)NULL;
		}
		(void) kstr_close(vp, -1);
		return (0);

	case CLR_AUTOPUSH:
		/* Remove autopush information */

		push.sap_cmd = SAP_CLEAR;
		push.sap_minor = *min;
		push.sap_major = *maj;

		if (error = kstr_ioctl(vp, SAD_SAP, (int)&push)) {
			printf("kstr_autopush: ioctl failed, error %d\n",
				error);
		}
		(void) kstr_close(vp, -1);
		return (error);

	case SET_AUTOPUSH:
		/* Set autopush information */

		if (*min == (minor_t)-1) {
			push.sap_cmd = SAP_ALL;
		} else if (*lastmin == 0) {
			push.sap_cmd = SAP_ONE;
		} else	{
			push.sap_cmd = SAP_RANGE;
		}

		push.sap_minor = *min;
		push.sap_major = *maj;
		if (lastmin)
			push.sap_lastminor = *lastmin;
		else	push.sap_lastminor = 0;

		/* pain */
		for (i = 0; i < MAXAPUSH && mods[i] != (char *)NULL; i++) {
			strcpy(push.sap_list[i], mods[i]);
		}
		push.sap_npush = i;
		push.sap_list[i][0] = (char)0;

		if (error = kstr_ioctl(vp, SAD_SAP, (int)&push)) {
			printf("kstr_autopush: ioctl failed, vp %x, error %d\n",
			    (int)vp, error);
		}
		(void) kstr_close(vp, -1);
		return (error);

	default:
		(void) kstr_close(vp, -1);
		return (EINVAL);
	}
}
