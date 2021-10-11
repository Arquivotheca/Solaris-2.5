/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)svc_gen.c	1.19	95/01/13 SMI"	/* SVr4.0 1.5	*/

/*
 *		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *	(c) 1986-1989, 1994, 1995 by Sun Microsystems, Inc.
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 */

/*
 * svc_generic.c,
 * Server side for RPC in the kernel.
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <rpc/types.h>
#include <netinet/in.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <sys/tiuser.h>
#include <sys/t_kuser.h>
#include <rpc/svc.h>
#include <sys/file.h>
#include <sys/user.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/fcntl.h>
#include <sys/errno.h>

extern  SVCXPRT * xprt_lookup();

extern	krwlock_t	xprt_lock;

/*
 * Called from nfs_svc() to associate a xprt handler with the stream (fp).
 * Detailed description:
 * 1. Make sure rpcmod is on the stream.
 * 2. Try to find an existing xprt for the stream.
 *    If found, increment the xprt xp_max_threads, and return.
 * 3. If cannot find xprt for the stream, call T_INFO_REQ to get the
 *    transport service type info.
 * 4. Call individual transport service type kcreate routine (svc_clts_kcreate,
 *    svc_cots_kcreate) to create and initialize the xprt for the stream.
 * 5. Call xprt_register to register the xprt and create the service thread.
 *
 */
int
svc_tli_kcreate(fp, max_msgsize, nxprt)
	register struct file	*fp;		/* connection end point */
	u_int			max_msgsize;	/* max receive size */
	SVCXPRT			**nxprt;
{
	queue_t		*wq;
	SVCXPRT		*xprt = NULL;		/* service handle */
	int		retval;
	struct strioctl strioc;
	struct T_info_ack	tinfo;
	int		error = 0;
	void		** vp;

	if (fp == NULL || nxprt == NULL)
		return (EINVAL);

	if (fp->f_vnode->v_stream == NULL)
		return (ENOSTR);

	/* Make sure that an RPC interface module is on the stream. */
	wq = fp->f_vnode->v_stream->sd_wrq;
	while ((wq = wq->q_next) != NULL) {
		if (strcmp(wq->q_qinfo->qi_minfo->mi_idname, "rpcmod") == 0)
			break;
	}
	if (!wq) {
		RPCLOG(1, "svc_tli_kcreate: no RPC module on stream\n", 0);
		return (EINVAL);
	}

	/* Check for an existing "master" xprt for this stream. */
	rw_enter(&xprt_lock, RW_READER);
	if (xprt = xprt_lookup(wq)) {
		/*
		 * If there is one, then just increment the maximum
		 * number of threads that can be created for the stream.
		 */
		xprt->xp_max_threads++;
		rw_exit(&xprt_lock);
		*nxprt = (SVCXPRT *)0;
		return (0);
	}
	rw_exit(&xprt_lock);

	/* Find out what type of transport this is. */
	strioc.ic_cmd = TI_GETINFO;
	strioc.ic_timout = -1;
	strioc.ic_len = sizeof (tinfo);
	strioc.ic_dp = (char *)&tinfo;
	tinfo.PRIM_type = T_INFO_REQ;

	error = strioctl(fp->f_vnode, I_STR, (int)&strioc, 0, K_TO_K,
			CRED(), &retval);
	if (error || retval) {
		RPCLOG(1, "svc_tli_kcreate: getinfo ioctl: %d\n", error);
		return (error);
	}

	/*
	 * call transport specific function.
	 */
	switch (tinfo.SERV_type) {
	case T_CLTS:
		error = svc_clts_kcreate(fp, max_msgsize, &tinfo, &xprt);
		break;
	case T_COTS:
	case T_COTS_ORD:
		error = svc_cots_kcreate(fp, max_msgsize, &tinfo, &xprt);
		break;
	default:
		RPCLOG(1, "svc_tli_kcreate: Bad service type %d\n",
			tinfo.SERV_type);
		error = EINVAL;
		break;
	}
	if (error != 0)
		return (error);

	xprt->xp_req_head = (mblk_t *)0;
	xprt->xp_req_tail = (mblk_t *)0;
	mutex_init(&xprt->xp_req_lock, "xprt request list", MUTEX_DEFAULT,
			DEFAULT_WT);
	mutex_init(&xprt->xp_lock, "xprt lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&xprt->xp_thread_lock, "xprt thread count lock",
			MUTEX_DEFAULT, DEFAULT_WT);
	cv_init(&xprt->xp_req_cv, "xprt request wait", CV_DEFAULT, NULL);
	xprt->xp_type = tinfo.SERV_type;
	xprt->xp_max_threads = 1;
	xprt->xp_min_threads = 1;
	xprt->xp_threads = 0;
	xprt->xp_detached_threads = 0;
	xprt->xp_asleep = 0;
	xprt->xp_drowsy = 0;
	xprt->xp_master = NULL;
	xprt->xp_cprp = NULL;
	xprt->xp_detached = FALSE;
	xprt->xp_fp = fp;
	xprt->xp_wq = wq;
	xprt->xp_closeproc = NULL;
	cv_init(&xprt->xp_dead_cv, "xprt dead wait", CV_DEFAULT, NULL);

	/*
	 * Set the private KRPC cell in the module's data.
	 */
	vp = (void **)wq->q_ptr;
	vp[0] = xprt;

	/*
	 * Register this transport handle after all fields
	 * have been initialized.
	 */
	xprt_register(xprt);
	*nxprt = xprt;

	return (0);

}

/*
 * Miscellaneous "control" functions for service transports.  This routine
 * implements all functions that are independent of the specific transport
 * type.  There are currently no transport-dependent control functions.  If
 * transport-dependent functions are added, the default branch of the
 * command switch should invoke the transport-dependent function via the
 * ops vector.
 */
bool_t
svc_control(SVCXPRT *xprt, u_int cmd, void *argp)
{
	bool_t result = FALSE;		/* be paranoid */

	ASSERT(xprt->xp_master == NULL); /* should already be the master */

	switch (cmd) {
	case SVCSET_CLOSEPROC:
		mutex_enter(&xprt->xp_lock);
		if (xprt->xp_wq == NULL) {
			/* transport is closing, too late to register */
			result = FALSE;
		} else {
			xprt->xp_closeproc = (void (*)(const SVCXPRT *))argp;
			result = TRUE;
		}
		mutex_exit(&xprt->xp_lock);
		break;
	default:
		cmn_err(CE_WARN, "svc_control: bad command (%d)",
			cmd);
		result = FALSE;
		break;
	}

	return (result);
}
