/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *		PROPRIETARY NOTICE (Combined)
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
 *	Copyright (c) 1986-1991,1993,1994, by Sun Microsystems, Inc.
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 */


#ifndef	_NFS_NFSSYS_H
#define	_NFS_NFSSYS_H

#pragma ident	"@(#)nfssys.h	1.21	94/12/09 SMI"	/* SVr4.0 1.5	*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Private definitions for the nfssys system call.
 * Note: <nfs/export.h> and <nfs/nfs.h> must be included before
 * this file.
 */

/*
 * Flavors of nfssys call.  Note that OLD_mumble commands are no longer
 * implemented, but the entries are kept as placeholders for binary
 * compatibility.
 */
enum nfssys_op	{ NFS_SVC, OLD_ASYNC_DAEMON, EXPORTFS, NFS_GETFH, OLD_NFS_CNVT,
		    NFS_REVAUTH, OLD_NFS_FH_TO_FID, LM_SVC, KILL_LOCKMGR };

struct nfs_svc_args {
	int	fd;
};

struct exportfs_args {
	char		*dname;
	struct export	*uex;
};

struct nfs_getfh_args {
	char		*fname;
	fhandle_t	*fhp;
};

struct nfs_revauth_args {
	int		authtype;
	uid_t		uid;
};


/*
 * Arguments for establishing lock manager service.  If you change
 * lm_svc_args, you should increment the version number.  Try to keep
 * supporting one or more old versions of the args, so that old lockd's
 * will work with new kernels.
 */

enum lm_fmly  { LM_INET, LM_LOOPBACK };
enum lm_proto { LM_TCP, LM_UDP };

struct lm_svc_args {
	int		version;	/* keep this first */
	int		fd;
	enum lm_fmly	n_fmly;		/* protocol family */
	enum lm_proto	n_proto;	/* protocol */
	dev_t		n_rdev;		/* device ID */
	int		debug;		/* debugging level */
	time_t		timout;		/* client handle life (asynch RPCs) */
	int		grace;		/* secs in grace period */
	time_t	retransmittimeout;	/* retransmission interval */
	int		max_threads;	/* max service threads for fd */
};

#define	LM_SVC_CUR_VERS	30		/* current lm_svc_args vers num */

#ifdef _KERNEL
union nfssysargs {
	struct exportfs_args	*exportfs_args_u;	/* exportfs args */
	struct nfs_getfh_args	*nfs_getfh_args_u;	/* nfs_getfh args */
	struct nfs_svc_args	*nfs_svc_args_u;	/* nfs_svc args */
	struct nfs_revauth_args	*nfs_revauth_args_u;	/* nfs_revauth args */
	struct lm_svc_args	*lm_svc_args_u;		/* lm_svc args */
						/* kill_lockmgr args: none */
};

struct nfssysa {
	enum nfssys_op		opcode;	/* operation discriminator */
	union nfssysargs	arg;	/* syscall-specific arg pointer */
};
#define	nfssysarg_exportfs	arg.exportfs_args_u
#define	nfssysarg_getfh		arg.nfs_getfh_args_u
#define	nfssysarg_svc		arg.nfs_svc_args_u
#define	nfssysarg_revauth	arg.nfs_revauth_args_u
#define	nfssysarg_lmsvc		arg.lm_svc_args_u
#endif	/* _KERNEL */

#ifdef _KERNEL

#include <sys/systm.h>		/* for rval_t typedef */

extern int	nfssys(register struct nfssysa *, rval_t *);
extern int	exportfs(struct exportfs_args *, cred_t *);
extern int	nfs_getfh(struct nfs_getfh_args *, cred_t *);
extern int	nfs_svc(struct nfs_svc_args *);
extern int	nfs_revoke_auth(int, uid_t, cred_t *);
extern int	lm_svc(struct lm_svc_args *uap);
extern int	lm_shutdown(void);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _NFS_NFSSYS_H */
