/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)nfs_sys.c	1.19	94/12/09 SMI"	/* SVr4.0 1.5	*/

/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *
 *
 *
 *		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	Copyright (c) 1986,1987,1988,1989,1993,1994, by Sun Microsystems, Inc.
 *  	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 */

#include <sys/types.h>
#include <rpc/types.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/siginfo.h>
#include <sys/proc.h>		/* for exit() declaration */
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <sys/thread.h>

/*ARGSUSED*/
int
nfssys(struct nfssysa *uap, rval_t *rvp)
{

	switch ((int) uap->opcode) {
		case NFS_SVC:
			/* NFS server daemon */
			{
				struct nfs_svc_args    nsa;

				/* export a file system */
				if (copyin((caddr_t) uap->nfssysarg_svc,
				    (caddr_t) &nsa, sizeof (nsa))) {
					return (EFAULT);
				} else {
					return (nfs_svc(&nsa));
				}
			}

		case EXPORTFS:
			/* export a file system */
			{
				struct exportfs_args    ea;
				int			result;

				/* export a file system */
				if (copyin((caddr_t) uap->nfssysarg_exportfs,
					    (caddr_t) &ea, sizeof (ea))) {
					return (EFAULT);
				} else {
					result = exportfs(&ea, CRED());
					return (result);
				}
			}

		case NFS_GETFH:
			/* get a file handle */
			{
				struct nfs_getfh_args	nga;
				int			result;

				/* export a file system */
				if (copyin((caddr_t) uap->nfssysarg_getfh,
				    (caddr_t) &nga, sizeof (nga))) {
					return (EFAULT);
				} else {
					result = nfs_getfh(&nga, CRED());
					return (result);
				}
			}

		case NFS_REVAUTH:
			/* revoke the cached credentials for the given uid */
			{
				struct nfs_revauth_args	nra;
				int			result;

				if (copyin((caddr_t) uap->nfssysarg_revauth,
					    (caddr_t) &nra, sizeof (nra))) {
					return (EFAULT);
				} else {
					result = nfs_revoke_auth(nra.authtype,
							nra.uid, CRED());
					return (result);
				}
			}

		case LM_SVC:
			/* LM server daemon */
			{
				struct lm_svc_args  lsa;

				/* start the lock manager server */
				if (copyin((caddr_t) uap->nfssysarg_lmsvc,
					(caddr_t) &lsa, sizeof (lsa)))
					return (EFAULT);
				else
					return (lm_svc(&lsa));
			}

		case KILL_LOCKMGR:
			return (lm_shutdown());

		default:
			return (EINVAL);
	}
}
