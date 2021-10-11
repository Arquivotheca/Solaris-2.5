/*
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's Unix(r) System V.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	Copyright (c) 1986-1992,1994,1995 by Sun Microsystems, Inc.
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 */

#pragma ident	"@(#)nfs_server.c	1.92	95/06/14 SMI"
/* SVr4.0 1.21 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <sys/siginfo.h>
#include <sys/tiuser.h>
#include <sys/statvfs.h>
#include <sys/t_kuser.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/dirent.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/unistd.h>
#include <sys/vtrace.h>
#include <sys/mode.h>
#include <sys/acl.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/auth_unix.h>
#include <rpc/auth_des.h>
#include <rpc/auth_kerb.h>
#include <rpc/svc.h>
#include <rpc/xdr.h>

#include <nfs/nfs.h>
#include <nfs/export.h>
#include <nfs/nfssys.h>
#include <nfs/nfs_clnt.h>
#include <nfs/nfs_acl.h>

#include <sys/modctl.h>

/*
 * Module linkage information.
 */
char _depends_on[] = "fs/nfs strmod/rpcmod";

static struct modlmisc modlmisc = {
	&mod_miscops, "NFS server module"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

_init(void)
{
	int status;

	if ((status = nfs_srvinit()) != 0) {
		cmn_err(CE_WARN, "_init: nfs_srvinit failed");
		return (status);
	}

	return (mod_install((struct modlinkage *)&modlinkage));
}

_fini()
{
	return (EBUSY);
}

_info(modinfop)
	struct modinfo *modinfop;
{

	return (mod_info(&modlinkage, modinfop));
}

/*
 * RPC dispatch table
 * Indexed by version, proc
 */

struct rpcdisp {
	void	  (*dis_proc)();	/* proc to call */
	xdrproc_t dis_xdrargs;		/* xdr routine to get args */
	xdrproc_t dis_fastxdrargs;	/* `fast' xdr routine to get args */
	int	  dis_argsz;		/* sizeof args */
	xdrproc_t dis_xdrres;		/* xdr routine to put results */
	xdrproc_t dis_fastxdrres;	/* `fast' xdr routine to put results */
	int	  dis_ressz;		/* size of results */
	void	  (*dis_resfree)();	/* frees space allocated by proc */
	int	  dis_flags;		/* flags, see below */
	fhandle_t *(*dis_getfh)();	/* returns the fhandle for the req */
};

#define	RPC_IDEMPOTENT	0x1	/* idempotent or not */
#define	RPC_ALLOWANON	0x2	/* allow anonymous access */
#define	RPC_MAPRESP	0x4	/* use mapped response buffer */
#define	RPC_AVOIDWORK	0x8	/* do work avoidance for dups */

struct rpc_disptable {
	int dis_nprocs;
#ifdef TRACE
	char **dis_procnames;
#endif
	kstat_named_t **dis_proccntp;
	struct rpcdisp *dis_table;
};

static void	rpc_null(caddr_t *, caddr_t *);
static void	rfs_error(caddr_t *, caddr_t *);
static void	nullfree(void);
static void	rfs_dispatch(struct svc_req *, SVCXPRT *);
static void	acl_dispatch(struct svc_req *, SVCXPRT *);
static int	rootname(struct export *, char *);
static int	kerbrootname(struct export *, struct authkerb_clnt_cred *);
static int	checkauth(struct exportinfo *, struct svc_req *, cred_t *, int);

/*
 * NFS Server system call.
 * Does all of the work of running a NFS server.
 * uap->fd is the fd of an open transport provider
 */
int
nfs_svc(struct nfs_svc_args *uap)
{
	file_t *fp;
	SVCXPRT *xprt;
	u_long vers;
	int error;
	int readsize;

	if (!suser(CRED()))
		return (EPERM);

	if ((fp = GETF(uap->fd)) == NULL)
		return (EBADF);

	/*
	 * Set read buffer size to rsize
	 * and add room for RPC headers.
	 */
	readsize = nfs3tsize() + (RPC_MAXDATASIZE - NFS_MAXDATA);
	if (readsize < RPC_MAXDATASIZE)
		readsize = RPC_MAXDATASIZE;

	/* Create a transport handle. */
	if ((error = svc_tli_kcreate(fp, readsize, &xprt)) != 0) {
		RELEASEF(uap->fd);
		return (error);
	}

	for (vers = NFS_VERSMIN; vers <= NFS_VERSMAX; vers++) {
		(void) svc_register(xprt, NFS_PROGRAM, vers,
				    rfs_dispatch, FALSE);
		(void) svc_kerb_reg(xprt, NFS_PROGRAM, vers, "nfs", "*", 0);
	}

	for (vers = NFS_ACL_VERSMIN; vers <= NFS_ACL_VERSMAX; vers++) {
		(void) svc_register(xprt, NFS_ACL_PROGRAM, vers,
				    acl_dispatch, FALSE);
		(void) svc_kerb_reg(xprt, NFS_ACL_PROGRAM, vers, "nfs", "*", 0);
	}


	RELEASEF(uap->fd);
	return (0);
}

/* ARGSUSED */
static void
rpc_null(caddr_t *argp, caddr_t *resp)
{
	/* do nothing */
	/* return (0); */
}

/* ARGSUSED */
static void
rfs_error(caddr_t *argp, caddr_t *resp)
{
	/* return (EOPNOTSUPP); */
}

static void
nullfree(void)
{
}

#ifdef TRACE
static char *rfscallnames_v2[] = {
	"RFS2_NULL",
	"RFS2_GETATTR",
	"RFS2_SETATTR",
	"RFS2_ROOT",
	"RFS2_LOOKUP",
	"RFS2_READLINK",
	"RFS2_READ",
	"RFS2_WRITECACHE",
	"RFS2_WRITE",
	"RFS2_CREATE",
	"RFS2_REMOVE",
	"RFS2_RENAME",
	"RFS2_LINK",
	"RFS2_SYMLINK",
	"RFS2_MKDIR",
	"RFS2_RMDIR",
	"RFS2_READDIR",
	"RFS2_STATFS"
};
#endif

static struct rpcdisp rfsdisptab_v2[] = {
	/*
	 * NFS VERSION 2
	 */

	/* RFS_NULL = 0 */
	{rpc_null,
	    xdr_void, 0, 0,
	    xdr_void, 0, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS_GETATTR = 1 */
	{rfs_getattr,
	    xdr_fhandle, xdr_fastfhandle, sizeof (fhandle_t),
#ifdef _LITTLE_ENDIAN
	    xdr_attrstat, xdr_fastattrstat, sizeof (struct nfsattrstat),
#else
	    xdr_attrstat, 0, sizeof (struct nfsattrstat),
#endif
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON|RPC_MAPRESP,
	    rfs_getattr_getfh},

	/* RFS_SETATTR = 2 */
	{rfs_setattr,
	    xdr_saargs, xdr_fastsaargs, sizeof (struct nfssaargs),
#ifdef _LITTLE_ENDIAN
	    xdr_attrstat, xdr_fastattrstat, sizeof (struct nfsattrstat),
#else
	    xdr_attrstat, 0, sizeof (struct nfsattrstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_setattr_getfh},

	/* RFS_ROOT = 3 *** NO LONGER SUPPORTED *** */
	{rfs_error,
	    xdr_void, 0, 0,
	    xdr_void, 0, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS_LOOKUP = 4 */
	{rfs_lookup,
	    xdr_diropargs, xdr_fastdiropargs, sizeof (struct nfsdiropargs),
#ifdef _LITTLE_ENDIAN
	    xdr_diropres, xdr_fastdiropres, sizeof (struct nfsdiropres),
#else
	    xdr_diropres, 0, sizeof (struct nfsdiropres),
#endif
	    nullfree, RPC_IDEMPOTENT|RPC_MAPRESP,
	    rfs_lookup_getfh},

	/* RFS_READLINK = 5 */
	{rfs_readlink,
	    xdr_fhandle, xdr_fastfhandle, sizeof (fhandle_t),
	    xdr_rdlnres, 0, sizeof (struct nfsrdlnres),
	    rfs_rlfree, RPC_IDEMPOTENT,
	    rfs_readlink_getfh},

	/* RFS_READ = 6 */
	{rfs_read,
	    xdr_readargs, xdr_fastreadargs, sizeof (struct nfsreadargs),
	    xdr_rdresult, 0, sizeof (struct nfsrdresult),
	    nullfree, RPC_IDEMPOTENT,
	    rfs_read_getfh},

	/* RFS_WRITECACHE = 7 *** NO LONGER SUPPORTED *** */
	{rfs_error,
	    xdr_void, 0, 0,
	    xdr_void, 0, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS_WRITE = 8 */
	{rfs_write,
	    xdr_writeargs, xdr_fastwriteargs, sizeof (struct nfswriteargs),
#ifdef _LITTLE_ENDIAN
	    xdr_attrstat, xdr_fastattrstat, sizeof (struct nfsattrstat),
#else
	    xdr_attrstat, 0, sizeof (struct nfsattrstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_write_getfh},

	/* RFS_CREATE = 9 */
	{rfs_create,
	    xdr_creatargs, xdr_fastcreatargs, sizeof (struct nfscreatargs),
#ifdef _LITTLE_ENDIAN
	    xdr_diropres, xdr_fastdiropres, sizeof (struct nfsdiropres),
#else
	    xdr_diropres, 0, sizeof (struct nfsdiropres),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_create_getfh},

	/* RFS_REMOVE = 10 */
	{rfs_remove,
	    xdr_diropargs, xdr_fastdiropargs, sizeof (struct nfsdiropargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, 0, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_remove_getfh},

	/* RFS_RENAME = 11 */
	{rfs_rename,
	    xdr_rnmargs, xdr_fastrnmargs, sizeof (struct nfsrnmargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, 0, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_rename_getfh},

	/* RFS_LINK = 12 */
	{rfs_link,
	    xdr_linkargs, xdr_fastlinkargs, sizeof (struct nfslinkargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, 0, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_link_getfh},

	/* RFS_SYMLINK = 13 */
	{rfs_symlink,
	    xdr_slargs, xdr_fastslargs, sizeof (struct nfsslargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, 0, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_symlink_getfh},

	/* RFS_MKDIR = 14 */
	{rfs_mkdir,
	    xdr_creatargs, xdr_fastcreatargs, sizeof (struct nfscreatargs),
#ifdef _LITTLE_ENDIAN
	    xdr_diropres, xdr_fastdiropres, sizeof (struct nfsdiropres),
#else
	    xdr_diropres, 0, sizeof (struct nfsdiropres),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_mkdir_getfh},

	/* RFS_RMDIR = 15 */
	{rfs_rmdir,
	    xdr_diropargs, xdr_fastdiropargs, sizeof (struct nfsdiropargs),
#ifdef _LITTLE_ENDIAN
	    xdr_enum, xdr_fastenum, sizeof (enum nfsstat),
#else
	    xdr_enum, 0, sizeof (enum nfsstat),
#endif
	    nullfree, RPC_MAPRESP,
	    rfs_rmdir_getfh},

	/* RFS_READDIR = 16 */
	{rfs_readdir,
	    xdr_rddirargs, xdr_fastrddirargs, sizeof (struct nfsrddirargs),
	    xdr_putrddirres, 0, sizeof (struct nfsrddirres),
	    rfs_rddirfree, RPC_IDEMPOTENT,
	    rfs_readdir_getfh},

	/* RFS_STATFS = 17 */
	{rfs_statfs,
	    xdr_fhandle, xdr_fastfhandle, sizeof (fhandle_t),
#ifdef _LITTLE_ENDIAN
	    xdr_statfs, xdr_faststatfs, sizeof (struct nfsstatfs),
#else
	    xdr_statfs, 0, sizeof (struct nfsstatfs),
#endif
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON|RPC_MAPRESP,
	    rfs_statfs_getfh},
};

#ifdef TRACE
static char *rfscallnames_v3[] = {
	"RFS3_NULL",
	"RFS3_GETATTR",
	"RFS3_SETATTR",
	"RFS3_LOOKUP",
	"RFS3_ACCESS",
	"RFS3_READLINK",
	"RFS3_READ",
	"RFS3_WRITE",
	"RFS3_CREATE",
	"RFS3_MKDIR",
	"RFS3_SYMLINK",
	"RFS3_MKNOD",
	"RFS3_REMOVE",
	"RFS3_RMDIR",
	"RFS3_RENAME",
	"RFS3_LINK",
	"RFS3_READDIR",
	"RFS3_READDIRPLUS",
	"RFS3_FSSTAT",
	"RFS3_FSINFO",
	"RFS3_PATHCONF",
	"RFS3_COMMIT"
};
#endif

static struct rpcdisp rfsdisptab_v3[] = {
	/*
	 * NFS VERSION 3
	 */

	/* RFS_NULL = 0 */
	{rpc_null,
	    xdr_void, 0, 0,
	    xdr_void, 0, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* RFS3_GETATTR = 1 */
	{rfs3_getattr,
	    xdr_GETATTR3args, 0, sizeof (GETATTR3args),
	    xdr_GETATTR3res, 0, sizeof (GETATTR3res),
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON,
	    rfs3_getattr_getfh},

	/* RFS3_SETATTR = 2 */
	{rfs3_setattr,
	    xdr_SETATTR3args, 0, sizeof (SETATTR3args),
	    xdr_SETATTR3res, 0, sizeof (SETATTR3res),
	    nullfree, 0,
	    rfs3_setattr_getfh},

	/* RFS3_LOOKUP = 3 */
	{rfs3_lookup,
	    xdr_LOOKUP3args, 0, sizeof (LOOKUP3args),
	    xdr_LOOKUP3res, 0, sizeof (LOOKUP3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_lookup_getfh},

	/* RFS3_ACCESS = 4 */
	{rfs3_access,
	    xdr_ACCESS3args, 0, sizeof (ACCESS3args),
	    xdr_ACCESS3res, 0, sizeof (ACCESS3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_access_getfh},

	/* RFS3_READLINK = 5 */
	{rfs3_readlink,
	    xdr_READLINK3args, 0, sizeof (READLINK3args),
	    xdr_READLINK3res, 0, sizeof (READLINK3res),
	    rfs3_readlink_free, RPC_IDEMPOTENT,
	    rfs3_readlink_getfh},

	/* RFS3_READ = 6 */
	{rfs3_read,
	    xdr_READ3args, 0, sizeof (READ3args),
	    xdr_READ3res, 0, sizeof (READ3res),
	    rfs3_read_free, RPC_IDEMPOTENT,
	    rfs3_read_getfh},

	/* RFS3_WRITE = 7 */
	{rfs3_write,
	    xdr_WRITE3args, 0, sizeof (WRITE3args),
	    xdr_WRITE3res, 0, sizeof (WRITE3res),
	    nullfree, 0,
	    rfs3_write_getfh},

	/* RFS3_CREATE = 8 */
	{rfs3_create,
	    xdr_CREATE3args, 0, sizeof (CREATE3args),
	    xdr_CREATE3res, 0, sizeof (CREATE3res),
	    nullfree, 0,
	    rfs3_create_getfh},

	/* RFS3_MKDIR = 9 */
	{rfs3_mkdir,
	    xdr_MKDIR3args, 0, sizeof (MKDIR3args),
	    xdr_MKDIR3res, 0, sizeof (MKDIR3res),
	    nullfree, 0,
	    rfs3_mkdir_getfh},

	/* RFS3_SYMLINK = 10 */
	{rfs3_symlink,
	    xdr_SYMLINK3args, 0, sizeof (SYMLINK3args),
	    xdr_SYMLINK3res, 0, sizeof (SYMLINK3res),
	    nullfree, 0,
	    rfs3_symlink_getfh},

	/* RFS3_MKNOD = 11 */
	{rfs3_mknod,
	    xdr_MKNOD3args, 0, sizeof (MKNOD3args),
	    xdr_MKNOD3res, 0, sizeof (MKNOD3res),
	    nullfree, 0,
	    rfs3_mknod_getfh},

	/* RFS3_REMOVE = 12 */
	{rfs3_remove,
	    xdr_REMOVE3args, 0, sizeof (REMOVE3args),
	    xdr_REMOVE3res, 0, sizeof (REMOVE3res),
	    nullfree, 0,
	    rfs3_remove_getfh},

	/* RFS3_RMDIR = 13 */
	{rfs3_rmdir,
	    xdr_RMDIR3args, 0, sizeof (RMDIR3args),
	    xdr_RMDIR3res, 0, sizeof (RMDIR3res),
	    nullfree, 0,
	    rfs3_rmdir_getfh},

	/* RFS3_RENAME = 14 */
	{rfs3_rename,
	    xdr_RENAME3args, 0, sizeof (RENAME3args),
	    xdr_RENAME3res, 0, sizeof (RENAME3res),
	    nullfree, 0,
	    rfs3_rename_getfh},

	/* RFS3_LINK = 15 */
	{rfs3_link,
	    xdr_LINK3args, 0, sizeof (LINK3args),
	    xdr_LINK3res, 0, sizeof (LINK3res),
	    nullfree, 0,
	    rfs3_link_getfh},

	/* RFS3_READDIR = 16 */
	{rfs3_readdir,
	    xdr_READDIR3args, 0, sizeof (READDIR3args),
	    xdr_READDIR3res, 0, sizeof (READDIR3res),
	    rfs3_readdir_free, RPC_IDEMPOTENT,
	    rfs3_readdir_getfh},

	/* RFS3_READDIRPLUS = 17 */
	{rfs3_readdirplus,
	    xdr_READDIRPLUS3args, 0, sizeof (READDIRPLUS3args),
	    xdr_READDIRPLUS3res, 0, sizeof (READDIRPLUS3res),
	    rfs3_readdirplus_free, RPC_AVOIDWORK,
	    rfs3_readdirplus_getfh},

	/* RFS3_FSSTAT = 18 */
	{rfs3_fsstat,
	    xdr_FSSTAT3args, 0, sizeof (FSSTAT3args),
	    xdr_FSSTAT3res, 0, sizeof (FSSTAT3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_fsstat_getfh},

	/* RFS3_FSINFO = 19 */
	{rfs3_fsinfo,
	    xdr_FSINFO3args, 0, sizeof (FSINFO3args),
	    xdr_FSINFO3res, 0, sizeof (FSINFO3res),
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON,
	    rfs3_fsinfo_getfh},

	/* RFS3_PATHCONF = 20 */
	{rfs3_pathconf,
	    xdr_PATHCONF3args, 0, sizeof (PATHCONF3args),
	    xdr_PATHCONF3res, 0, sizeof (PATHCONF3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_pathconf_getfh},

	/* RFS3_COMMIT = 21 */
	{rfs3_commit,
	    xdr_COMMIT3args, 0, sizeof (COMMIT3args),
	    xdr_COMMIT3res, 0, sizeof (COMMIT3res),
	    nullfree, RPC_IDEMPOTENT,
	    rfs3_commit_getfh},
};

union rfs_args {
	/*
	 * NFS VERSION 2
	 */

	/* RFS_NULL = 0 */

	/* RFS_GETATTR = 1 */
	fhandle_t nfs2_getattr_args;

	/* RFS_SETATTR = 2 */
	struct nfssaargs nfs2_setattr_args;

	/* RFS_ROOT = 3 *** NO LONGER SUPPORTED *** */

	/* RFS_LOOKUP = 4 */
	struct nfsdiropargs nfs2_lookup_args;

	/* RFS_READLINK = 5 */
	fhandle_t nfs2_readlink_args;

	/* RFS_READ = 6 */
	struct nfsreadargs nfs2_read_args;

	/* RFS_WRITECACHE = 7 *** NO LONGER SUPPORTED *** */

	/* RFS_WRITE = 8 */
	struct nfswriteargs nfs2_write_args;

	/* RFS_CREATE = 9 */
	struct nfscreatargs nfs2_create_args;

	/* RFS_REMOVE = 10 */
	struct nfsdiropargs nfs2_remove_args;

	/* RFS_RENAME = 11 */
	struct nfsrnmargs nfs2_rename_args;

	/* RFS_LINK = 12 */
	struct nfslinkargs nfs2_link_args;

	/* RFS_SYMLINK = 13 */
	struct nfsslargs nfs2_symlink_args;

	/* RFS_MKDIR = 14 */
	struct nfscreatargs nfs2_mkdir_args;

	/* RFS_RMDIR = 15 */
	struct nfsdiropargs nfs2_rmdir_args;

	/* RFS_READDIR = 16 */
	struct nfsrddirargs nfs2_readdir_args;

	/* RFS_STATFS = 17 */
	fhandle_t nfs2_statfs_args;

	/*
	 * NFS VERSION 3
	 */

	/* RFS_NULL = 0 */

	/* RFS3_GETATTR = 1 */
	GETATTR3args nfs3_getattr_args;

	/* RFS3_SETATTR = 2 */
	SETATTR3args nfs3_setattr_args;

	/* RFS3_LOOKUP = 3 */
	LOOKUP3args nfs3_lookup_args;

	/* RFS3_ACCESS = 4 */
	ACCESS3args nfs3_access_args;

	/* RFS3_READLINK = 5 */
	READLINK3args nfs3_readlink_args;

	/* RFS3_READ = 6 */
	READ3args nfs3_read_args;

	/* RFS3_WRITE = 7 */
	WRITE3args nfs3_write_args;

	/* RFS3_CREATE = 8 */
	CREATE3args nfs3_create_args;

	/* RFS3_MKDIR = 9 */
	MKDIR3args nfs3_mkdir_args;

	/* RFS3_SYMLINK = 10 */
	SYMLINK3args nfs3_symlink_args;

	/* RFS3_MKNOD = 11 */
	MKNOD3args nfs3_mknod_args;

	/* RFS3_REMOVE = 12 */
	REMOVE3args nfs3_remove_args;

	/* RFS3_RMDIR = 13 */
	RMDIR3args nfs3_rmdir_args;

	/* RFS3_RENAME = 14 */
	RENAME3args nfs3_rename_args;

	/* RFS3_LINK = 15 */
	LINK3args nfs3_link_args;

	/* RFS3_READDIR = 16 */
	READDIR3args nfs3_readdir_args;

	/* RFS3_READDIRPLUS = 17 */
	READDIRPLUS3args nfs3_readdirplus_args;

	/* RFS3_FSSTAT = 18 */
	FSSTAT3args nfs3_fsstat_args;

	/* RFS3_FSINFO = 19 */
	FSINFO3args nfs3_fsinfo_args;

	/* RFS3_PATHCONF = 20 */
	PATHCONF3args nfs3_pathconf_args;

	/* RFS3_COMMIT = 21 */
	COMMIT3args nfs3_commit_args;
};

union rfs_res {
	/*
	 * NFS VERSION 2
	 */

	/* RFS_NULL = 0 */

	/* RFS_GETATTR = 1 */
	struct nfsattrstat nfs2_getattr_res;

	/* RFS_SETATTR = 2 */
	struct nfsattrstat nfs2_setattr_res;

	/* RFS_ROOT = 3 *** NO LONGER SUPPORTED *** */

	/* RFS_LOOKUP = 4 */
	struct nfsdiropres nfs2_lookup_res;

	/* RFS_READLINK = 5 */
	struct nfsrdlnres nfs2_readlink_res;

	/* RFS_READ = 6 */
	struct nfsrdresult nfs2_read_res;

	/* RFS_WRITECACHE = 7 *** NO LONGER SUPPORTED *** */

	/* RFS_WRITE = 8 */
	struct nfsattrstat nfs2_write_res;

	/* RFS_CREATE = 9 */
	struct nfsdiropres nfs2_create_res;

	/* RFS_REMOVE = 10 */
	enum nfsstat nfs2_remove_res;

	/* RFS_RENAME = 11 */
	enum nfsstat nfs2_rename_res;

	/* RFS_LINK = 12 */
	enum nfsstat nfs2_link_res;

	/* RFS_SYMLINK = 13 */
	enum nfsstat nfs2_symlink_res;

	/* RFS_MKDIR = 14 */
	struct nfsdiropres nfs2_mkdir_res;

	/* RFS_RMDIR = 15 */
	enum nfsstat nfs2_rmdir_res;

	/* RFS_READDIR = 16 */
	struct nfsrddirres nfs2_readdir_res;

	/* RFS_STATFS = 17 */
	struct nfsstatfs nfs2_statfs_res;

	/*
	 * NFS VERSION 3
	 */

	/* RFS_NULL = 0 */

	/* RFS3_GETATTR = 1 */
	GETATTR3res nfs3_getattr_res;

	/* RFS3_SETATTR = 2 */
	SETATTR3res nfs3_setattr_res;

	/* RFS3_LOOKUP = 3 */
	LOOKUP3res nfs3_lookup_res;

	/* RFS3_ACCESS = 4 */
	ACCESS3res nfs3_access_res;

	/* RFS3_READLINK = 5 */
	READLINK3res nfs3_readlink_res;

	/* RFS3_READ = 6 */
	READ3res nfs3_read_res;

	/* RFS3_WRITE = 7 */
	WRITE3res nfs3_write_res;

	/* RFS3_CREATE = 8 */
	CREATE3res nfs3_create_res;

	/* RFS3_MKDIR = 9 */
	MKDIR3res nfs3_mkdir_res;

	/* RFS3_SYMLINK = 10 */
	SYMLINK3res nfs3_symlink_res;

	/* RFS3_MKNOD = 11 */
	MKNOD3res nfs3_mknod_res;

	/* RFS3_REMOVE = 12 */
	REMOVE3res nfs3_remove_res;

	/* RFS3_RMDIR = 13 */
	RMDIR3res nfs3_rmdir_res;

	/* RFS3_RENAME = 14 */
	RENAME3res nfs3_rename_res;

	/* RFS3_LINK = 15 */
	LINK3res nfs3_link_res;

	/* RFS3_READDIR = 16 */
	READDIR3res nfs3_readdir_res;

	/* RFS3_READDIRPLUS = 17 */
	READDIRPLUS3res nfs3_readdirplus_res;

	/* RFS3_FSSTAT = 18 */
	FSSTAT3res nfs3_fsstat_res;

	/* RFS3_FSINFO = 19 */
	FSINFO3res nfs3_fsinfo_res;

	/* RFS3_PATHCONF = 20 */
	PATHCONF3res nfs3_pathconf_res;

	/* RFS3_COMMIT = 21 */
	COMMIT3res nfs3_commit_res;
};

static struct rpc_disptable rfs_disptable[] = {
	{sizeof (rfsdisptab_v2) / sizeof (rfsdisptab_v2[0]),
#ifdef TRACE
		rfscallnames_v2,
#endif
		&rfsproccnt_v2_ptr, rfsdisptab_v2},
	{sizeof (rfsdisptab_v3) / sizeof (rfsdisptab_v3[0]),
#ifdef TRACE
		rfscallnames_v3,
#endif
		&rfsproccnt_v3_ptr, rfsdisptab_v3},
};

/*
 *	If nfs_portmon is set, then clients are required to use privileged
 *	ports (ports < IPPORT_RESERVED) in order to get NFS services.
 *
 *	N.B.:  this attempt to carry forward the already ill-conceived
 *	notion of privileged ports for TCP/UDP is really quite ineffectual.
 *	Not only is it transport-dependent, it's laughably easy to spoof.
 *	If you're really interested in security, you must start with secure
 *	RPC instead.
 */
static int nfs_portmon = 0;

#ifdef TRACE
struct udp_data {
	u_long	ud_xid;				/* id */
	mblk_t	*ud_resp;			/* buffer for response */
	mblk_t	*ud_inmp;			/* mblk chain of request */
	XDR	ud_xdrin;			/* input xdr stream */
	XDR	ud_xdrout;			/* output xdr stream */
};
#define	REQTOXID(req)   ((struct udp_data *)((req)->rq_xprt->xp_p2))->ud_xid
#endif

#ifdef DEBUG
static int cred_hits = 0;
static int cred_misses = 0;
#endif

#ifdef DEBUG
/*
 * Debug code to allow disabling of rfs_dispatch() use of
 * fastxdrargs() and fastxdrres() calls for testing purposes.
 */
static int rfs_no_fast_xdrargs = 0;
static int rfs_no_fast_xdrres = 0;
#endif

static void
rfs_dispatch(struct svc_req *req, SVCXPRT *xprt)
{
	int which;
	u_long vers;
	char *args;
	union rfs_args args_buf;
	char *res;
	union rfs_res res_buf;
	register struct rpcdisp *disp;
	cred_t *cr;
	int error = 0;
	int anon_ok;
	struct exportinfo *exi = NULL;
	register int dupstat;
	struct dupreq *dr;

	vers = req->rq_vers;
	if (vers < NFS_VERSMIN || vers > NFS_VERSMAX) {
		TRACE_3(TR_FAC_NFS, TR_RFS_DISPATCH_START,
			"rfs_dispatch_start:(%S) proc_num %d xid %x",
			"bad version", (int)vers, 0);
		svcerr_progvers(req->rq_xprt, NFS_VERSMIN, NFS_VERSMAX);
		error++;
		cmn_err(CE_NOTE, "nfs_server: bad version number %lu", vers);
		goto end;
	}
	vers -= NFS_VERSMIN;

	which = req->rq_proc;
	if (which < 0 || which >= rfs_disptable[(int)vers].dis_nprocs) {
		TRACE_3(TR_FAC_NFS, TR_RFS_DISPATCH_START,
			"rfs_dispatch_start:(%S) proc_num %d xid %x",
			"bad proc", which, 0);
		svcerr_noproc(req->rq_xprt);
		error++;
		cmn_err(CE_NOTE, "nfs_server: bad proc number %d", which);
		goto end;
	}
	TRACE_3(TR_FAC_NFS, TR_RFS_DISPATCH_START,
		"rfs_dispatch_start:(%S) proc_num %d xid %x",
		rfs_disptable[(int)vers].dis_procnames[which], which,
		REQTOXID(req));

	(*(rfs_disptable[(int)vers].dis_proccntp))[which].value.ul++;

	disp = &rfs_disptable[(int)vers].dis_table[which];

	/*
	 * Deserialize into the args struct.
	 */
	args = (char *)&args_buf;
	TRACE_0(TR_FAC_NFS, TR_SVC_GETARGS_START,
		"svc_getargs_start:");
#ifdef DEBUG
	if (rfs_no_fast_xdrargs || disp->dis_fastxdrargs == NULL ||
	    ! SVC_GETARGS(xprt, disp->dis_fastxdrargs, (char *)&args)) {
#else
	if (disp->dis_fastxdrargs == NULL ||
	    ! SVC_GETARGS(xprt, disp->dis_fastxdrargs, (char *)&args)) {
#endif
		bzero(args, (u_int)disp->dis_argsz);
		if (! SVC_GETARGS(xprt, disp->dis_xdrargs, args)) {
			TRACE_1(TR_FAC_NFS, TR_SVC_GETARGS_END,
				"svc_getargs_end:(%S)", "bad");
			svcerr_decode(xprt);
			error++;
			cmn_err(CE_NOTE, "nfs_server: bad getargs for %lu/%d",
				vers + NFS_VERSMIN, which);
			goto done;
		}
	}
	TRACE_1(TR_FAC_NFS, TR_SVC_GETARGS_END,
		"svc_getargs_end:(%S)", "good");

	/*
	 * Find export information and check authentication,
	 * setting the credential if everything is ok.
	 */
	if (disp->dis_getfh != NULL) {
		fhandle_t *fh;

		fh = (*disp->dis_getfh)(args);

		/*
		 * Fix for bug 1038302 - corbin
		 * There is a problem here if anonymous access is
		 * disallowed.  If the current request is part of the
		 * client's mount process for the requested filesystem,
		 * then it will carry root (uid 0) credentials on it, and
		 * will be denied by checkauth if that client does not
		 * have explicit root=0 permission.  This will cause the
		 * client's mount operation to fail.  As a work-around,
		 * we check here to see if the request is a getattr or
		 * statfs operation on the exported vnode itself, and
		 * pass a flag to checkauth with the result of this test.
		 *
		 * The filehandle refers to the mountpoint itself if
		 * the fh_data and fh_xdata portions of the filehandle
		 * are equal.
		 */

		if ((disp->dis_flags & RPC_ALLOWANON) &&
		    EQFID((fid_t *)&fh->fh_len, (fid_t *)&fh->fh_xlen))
			anon_ok = 1;
		else
			anon_ok = 0;

		cr = xprt->xp_cred;
		ASSERT(cr != NULL);
#ifdef DEBUG
		if (cr->cr_ref != 1) {
			crfree(cr);
			cr = crget();
			xprt->xp_cred = cr;
			cred_misses++;
		} else
			cred_hits++;
#else
		if (cr->cr_ref != 1) {
			crfree(cr);
			cr = crget();
			xprt->xp_cred = cr;
		}
#endif

		TRACE_0(TR_FAC_NFS, TR_FINDEXPORT_START,
			"findexport_start:");
		exi = findexport(&fh->fh_fsid, (fid_t *) &fh->fh_xlen);
		TRACE_0(TR_FAC_NFS, TR_FINDEXPORT_END,
			"findexport_end:");
		if (exi != NULL && !checkauth(exi, req, cr, anon_ok)) {
			svcerr_weakauth(xprt);
			error++;
			cmn_err(CE_NOTE, "nfs_server: weak authentication");
			goto done;
		}
	} else
		cr = NULL;

	if (disp->dis_flags & RPC_MAPRESP) {
		res = (char *)SVC_GETRES(xprt, disp->dis_ressz);
		if (res == NULL)
			res = (char *)&res_buf;
	} else
		res = (char *)&res_buf;

	if (!(disp->dis_flags & RPC_IDEMPOTENT)) {
		dupstat = SVC_DUP(xprt, req, res, disp->dis_ressz, &dr);

		switch (dupstat) {
		case DUP_ERROR:
			svcerr_systemerr(xprt);
			error++;
			goto done;
			/* NOTREACHED */
		case DUP_INPROGRESS:
			if (res != (char *)&res_buf)
				SVC_FREERES(xprt);
			error++;
			goto done;
			/* NOTREACHED */
		case DUP_NEW:
		case DUP_DROP:
			curthread->t_flag |= T_DONTPEND;
			TRACE_3(TR_FAC_NFS, TR_RFS_PROC_START,
				"rfs_proc_start:(%S) proc_num %d xid %x",
				rfs_disptable[(int)vers].dis_procnames[which],
				which, REQTOXID(req));
			(*disp->dis_proc)(args, res, exi, req, cr);
			TRACE_0(TR_FAC_NFS, TR_RFS_PROC_END,
				"rfs_proc_end:");
			curthread->t_flag &= ~T_DONTPEND;
			if (curthread->t_flag & T_WOULDBLOCK) {
				curthread->t_flag &= ~T_WOULDBLOCK;
				SVC_DUPDONE(xprt, dr, res, disp->dis_ressz,
					    DUP_DROP);
				if (res != (char *)&res_buf)
					SVC_FREERES(xprt);
				error++;
				goto done;
			}
			if (disp->dis_flags & RPC_AVOIDWORK) {
				SVC_DUPDONE(xprt, dr, res, disp->dis_ressz,
					    DUP_DROP);
			} else {
				SVC_DUPDONE(xprt, dr, res, disp->dis_ressz,
					    DUP_DONE);
			}
			break;
		case DUP_DONE:
			break;
		}

	} else {
		curthread->t_flag |= T_DONTPEND;
		TRACE_3(TR_FAC_NFS, TR_RFS_PROC_START,
			"rfs_proc_start:(%S) proc_num %d xid %x",
			rfs_disptable[(int)vers].dis_procnames[which], which,
			REQTOXID(req));
		(*disp->dis_proc)(args, res, exi, req, cr);
		TRACE_0(TR_FAC_NFS, TR_RFS_PROC_END,
			"rfs_proc_end:");
		curthread->t_flag &= ~T_DONTPEND;
		if (curthread->t_flag & T_WOULDBLOCK) {
			curthread->t_flag &= ~T_WOULDBLOCK;
			if (res != (char *)&res_buf)
				SVC_FREERES(xprt);
			error++;
			goto done;
		}
	}

	/*
	 * Serialize and send results struct
	 */
	TRACE_0(TR_FAC_NFS, TR_SVC_SENDREPLY_START,
		"svc_sendreply_start:");
#ifdef DEBUG
	if (rfs_no_fast_xdrres == 0 && res != (char *)&res_buf) {
#else
	if (res != (char *)&res_buf) {
#endif
		if (!svc_sendreply(xprt, disp->dis_fastxdrres, res)) {
			cmn_err(CE_NOTE, "nfs_server: bad sendreply");
			error++;
		}
	} else {
		if (!svc_sendreply(xprt, disp->dis_xdrres, res)) {
			cmn_err(CE_NOTE, "nfs_server: bad sendreply");
			error++;
		}
	}
	TRACE_0(TR_FAC_NFS, TR_SVC_SENDREPLY_END,
		"svc_sendreply_end:");

	/*
	 * Free results struct
	 */
	if (disp->dis_resfree != nullfree) {
		TRACE_0(TR_FAC_NFS, TR_SVC_FREERES_START,
			"svc_freeres_start:");
		(*disp->dis_resfree)(res);
		TRACE_0(TR_FAC_NFS, TR_SVC_FREERES_END,
			"svc_freeres_end:");
	}

done:
	/*
	 * Free arguments struct
	 */
	TRACE_0(TR_FAC_NFS, TR_SVC_FREEARGS_START,
		"svc_freeargs_start:");
	if (!SVC_FREEARGS(xprt, disp->dis_xdrargs, args)) {
		cmn_err(CE_NOTE, "nfs_server: bad freeargs");
		error++;
	}
	TRACE_0(TR_FAC_NFS, TR_SVC_FREEARGS_END,
		"svc_freeargs_end:");

	if (exi != NULL)
		export_rw_exit();

end:
	svstat_ptr[NFS_BADCALLS].value.ul += error;
	svstat_ptr[NFS_CALLS].value.ul++;

	TRACE_1(TR_FAC_NFS, TR_RFS_DISPATCH_END,
		"rfs_dispatch_end:proc_num %d",
		which);
}

#ifdef TRACE
static char *aclcallnames_v2[] = {
	"ACL2_NULL",
	"ACL2_GETACL",
	"ACL2_SETACL",
	"ACL2_GETATTR",
	"ACL2_ACCESS"
};
#endif

static struct rpcdisp acldisptab_v2[] = {
	/*
	 * ACL VERSION 2
	 */

	/* ACL2_NULL = 0 */
	{rpc_null,
	    xdr_void, 0, 0,
	    xdr_void, 0, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* ACL2_GETACL = 1 */
	{acl2_getacl,
	    xdr_GETACL2args, xdr_fastGETACL2args, sizeof (GETACL2args),
	    xdr_GETACL2res, 0, sizeof (GETACL2res),
	    acl2_getacl_free, RPC_IDEMPOTENT,
	    acl2_getacl_getfh},

	/* ACL2_SETACL = 2 */
	{acl2_setacl,
	    xdr_SETACL2args, 0, sizeof (SETACL2args),
#ifdef _LITTLE_ENDIAN
	    xdr_SETACL2res, xdr_fastSETACL2res, sizeof (SETACL2res),
#else
	    xdr_SETACL2res, 0, sizeof (SETACL2res),
#endif
	    nullfree, RPC_MAPRESP,
	    acl2_setacl_getfh},

	/* ACL2_GETATTR = 3 */
	{acl2_getattr,
	    xdr_GETATTR2args, xdr_fastGETATTR2args, sizeof (GETATTR2args),
#ifdef _LITTLE_ENDIAN
	    xdr_GETATTR2res, xdr_fastGETATTR2res, sizeof (GETATTR2res),
#else
	    xdr_GETATTR2res, 0, sizeof (GETATTR2res),
#endif
	    nullfree, RPC_IDEMPOTENT|RPC_ALLOWANON|RPC_MAPRESP,
	    acl2_getattr_getfh},

	/* ACL2_ACCESS = 4 */
	{acl2_access,
	    xdr_ACCESS2args, xdr_fastACCESS2args, sizeof (ACCESS2args),
#ifdef _LITTLE_ENDIAN
	    xdr_ACCESS2res, xdr_fastACCESS2res, sizeof (ACCESS2res),
#else
	    xdr_ACCESS2res, 0, sizeof (ACCESS2res),
#endif
	    nullfree, RPC_IDEMPOTENT|RPC_MAPRESP,
	    acl2_access_getfh},
};

#ifdef TRACE
static char *aclcallnames_v3[] = {
	"ACL3_NULL",
	"ACL3_GETACL",
	"ACL3_SETACL"
};
#endif

static struct rpcdisp acldisptab_v3[] = {
	/*
	 * ACL VERSION 3
	 */

	/* ACL3_NULL = 0 */
	{rpc_null,
	    xdr_void, 0, 0,
	    xdr_void, 0, 0,
	    nullfree, RPC_IDEMPOTENT,
	    0},

	/* ACL3_GETACL = 1 */
	{acl3_getacl,
	    xdr_GETACL3args, 0, sizeof (GETACL3args),
	    xdr_GETACL3res, 0, sizeof (GETACL3res),
	    acl3_getacl_free, RPC_IDEMPOTENT,
	    acl3_getacl_getfh},

	/* ACL3_SETACL = 2 */
	{acl3_setacl,
	    xdr_SETACL3args, 0, sizeof (SETACL3args),
	    xdr_SETACL3res, 0, sizeof (SETACL3res),
	    nullfree, 0,
	    acl3_setacl_getfh},
};

union acl_args {
	/*
	 * ACL VERSION 2
	 */

	/* ACL2_NULL = 0 */

	/* ACL2_GETACL = 1 */
	GETACL2args acl2_getacl_args;

	/* ACL2_SETACL = 2 */
	SETACL2args acl2_setacl_args;

	/* ACL2_GETATTR = 3 */
	GETATTR2args acl2_getattr_args;

	/* ACL2_ACCESS = 3 */
	ACCESS2args acl2_access_args;

	/*
	 * ACL VERSION 3
	 */

	/* ACL3_NULL = 0 */

	/* ACL3_GETACL = 1 */
	GETACL3args acl3_getacl_args;

	/* ACL3_SETACL = 2 */
	SETACL3args acl3_setacl;
};

union acl_res {
	/*
	 * ACL VERSION 2
	 */

	/* ACL2_NULL = 0 */

	/* ACL2_GETACL = 1 */
	GETACL2res acl2_getacl_res;

	/* ACL2_SETACL = 2 */
	SETACL2res acl2_setacl_res;

	/* ACL2_GETATTR = 3 */
	GETATTR2res acl2_getattr_res;

	/* ACL2_ACCESS = 3 */
	ACCESS2res acl2_access_res;

	/*
	 * ACL VERSION 3
	 */

	/* ACL3_NULL = 0 */

	/* ACL3_GETACL = 1 */
	GETACL3res acl3_getacl_res;

	/* ACL3_SETACL = 2 */
	SETACL3res acl3_setacl;
};

static struct rpc_disptable acl_disptable[] = {
	{sizeof (acldisptab_v2) / sizeof (acldisptab_v2[0]),
#ifdef TRACE
		aclcallnames_v2,
#endif
		&aclproccnt_v2_ptr, acldisptab_v2},
	{sizeof (acldisptab_v3) / sizeof (acldisptab_v3[0]),
#ifdef TRACE
		aclcallnames_v3,
#endif
		&aclproccnt_v3_ptr, acldisptab_v3},
};

static void
acl_dispatch(struct svc_req *req, SVCXPRT *xprt)
{
	int which;
	u_long vers;
	char *args;
	union acl_args args_buf;
	char *res;
	union acl_res res_buf;
	register struct rpcdisp *disp;
	cred_t *cr;
	int error = 0;
	int anon_ok;
	struct exportinfo *exi = NULL;
	register int dupstat;
	struct dupreq *dr;

	vers = req->rq_vers;
	if (vers < NFS_ACL_VERSMIN || vers > NFS_ACL_VERSMAX) {
		svcerr_progvers(req->rq_xprt, NFS_ACL_VERSMIN, NFS_ACL_VERSMAX);
		error++;
		cmn_err(CE_NOTE, "acl_server: bad version number %lu", vers);
		goto end;
	}
	vers -= NFS_ACL_VERSMIN;

	which = req->rq_proc;
	if (which < 0 || which >= acl_disptable[(int)vers].dis_nprocs) {
		svcerr_noproc(req->rq_xprt);
		error++;
		cmn_err(CE_NOTE, "acl_server: bad proc number %d", which);
		goto end;
	}

	(*(acl_disptable[(int)vers].dis_proccntp))[which].value.ul++;

	disp = &acl_disptable[(int)vers].dis_table[which];

	/*
	 * Deserialize into the args struct.
	 */
	args = (char *)&args_buf;
#ifdef DEBUG
	if (rfs_no_fast_xdrargs || disp->dis_fastxdrargs == NULL ||
	    ! SVC_GETARGS(xprt, disp->dis_fastxdrargs, (char *)&args)) {
#else
	if (disp->dis_fastxdrargs == NULL ||
	    ! SVC_GETARGS(xprt, disp->dis_fastxdrargs, (char *)&args)) {
#endif
		bzero(args, (u_int)disp->dis_argsz);
		if (! SVC_GETARGS(xprt, disp->dis_xdrargs, args)) {
			svcerr_decode(xprt);
			error++;
			cmn_err(CE_NOTE, "acl_server: bad getargs for %lu/%d",
				vers + NFS_ACL_VERSMIN, which);
			goto done;
		}
	}

	/*
	 * Find export information and check authentication,
	 * setting the credential if everything is ok.
	 */
	if (disp->dis_getfh != NULL) {
		fhandle_t *fh;

		fh = (*disp->dis_getfh)(args);

		/*
		 * Fix for bug 1038302 - corbin
		 * There is a problem here if anonymous access is
		 * disallowed.  If the current request is part of the
		 * client's mount process for the requested filesystem,
		 * then it will carry root (uid 0) credentials on it, and
		 * will be denied by checkauth if that client does not
		 * have explicit root=0 permission.  This will cause the
		 * client's mount operation to fail.  As a work-around,
		 * we check here to see if the request is a getattr or
		 * statfs operation on the exported vnode itself, and
		 * pass a flag to checkauth with the result of this test.
		 *
		 * The filehandle refers to the mountpoint itself if
		 * the fh_data and fh_xdata portions of the filehandle
		 * are equal.
		 */

		if ((disp->dis_flags & RPC_ALLOWANON) &&
		    EQFID((fid_t *)&fh->fh_len, (fid_t *)&fh->fh_xlen))
			anon_ok = 1;
		else
			anon_ok = 0;

		cr = xprt->xp_cred;
		ASSERT(cr != NULL);
#ifdef DEBUG
		if (cr->cr_ref != 1) {
			crfree(cr);
			cr = crget();
			xprt->xp_cred = cr;
			cred_misses++;
		} else
			cred_hits++;
#else
		if (cr->cr_ref != 1) {
			crfree(cr);
			cr = crget();
			xprt->xp_cred = cr;
		}
#endif

		exi = findexport(&fh->fh_fsid, (fid_t *) &fh->fh_xlen);
		if (exi != NULL && !checkauth(exi, req, cr, anon_ok)) {
			svcerr_weakauth(xprt);
			error++;
			cmn_err(CE_NOTE, "acl_server: weak authentication");
			goto done;
		}
	} else
		cr = NULL;

	if (disp->dis_flags & RPC_MAPRESP) {
		res = (char *)SVC_GETRES(xprt, disp->dis_ressz);
		if (res == NULL)
			res = (char *)&res_buf;
	} else
		res = (char *)&res_buf;

	if (!(disp->dis_flags & RPC_IDEMPOTENT)) {
		dupstat = SVC_DUP(xprt, req, res, disp->dis_ressz, &dr);

		switch (dupstat) {
		case DUP_ERROR:
			svcerr_systemerr(xprt);
			error++;
			goto done;
			/* NOTREACHED */
		case DUP_INPROGRESS:
			if (res != (char *)&res_buf)
				SVC_FREERES(xprt);
			error++;
			goto done;
			/* NOTREACHED */
		case DUP_NEW:
		case DUP_DROP:
			curthread->t_flag |= T_DONTPEND;
			(*disp->dis_proc)(args, res, exi, req, cr);
			curthread->t_flag &= ~T_DONTPEND;
			if (curthread->t_flag & T_WOULDBLOCK) {
				curthread->t_flag &= ~T_WOULDBLOCK;
				SVC_DUPDONE(xprt, dr, res, disp->dis_ressz,
					    DUP_DROP);
				if (res != (char *)&res_buf)
					SVC_FREERES(xprt);
				error++;
				goto done;
			}
			if (disp->dis_flags & RPC_AVOIDWORK) {
				SVC_DUPDONE(xprt, dr, res, disp->dis_ressz,
					    DUP_DROP);
			} else {
				SVC_DUPDONE(xprt, dr, res, disp->dis_ressz,
					    DUP_DONE);
			}
			break;
		case DUP_DONE:
			break;
		}

	} else {
		curthread->t_flag |= T_DONTPEND;
		(*disp->dis_proc)(args, res, exi, req, cr);
		curthread->t_flag &= ~T_DONTPEND;
		if (curthread->t_flag & T_WOULDBLOCK) {
			curthread->t_flag &= ~T_WOULDBLOCK;
			if (res != (char *)&res_buf)
				SVC_FREERES(xprt);
			error++;
			goto done;
		}
	}

	/*
	 * Serialize and send results struct
	 */
#ifdef DEBUG
	if (rfs_no_fast_xdrres == 0 && res != (char *)&res_buf) {
#else
	if (res != (char *)&res_buf) {
#endif
		if (!svc_sendreply(xprt, disp->dis_fastxdrres, res)) {
			cmn_err(CE_NOTE, "acl_server: bad sendreply");
			error++;
		}
	} else {
		if (!svc_sendreply(xprt, disp->dis_xdrres, res)) {
			cmn_err(CE_NOTE, "acl_server: bad sendreply");
			error++;
		}
	}

	/*
	 * Free results struct
	 */
	if (disp->dis_resfree != nullfree)
		(*disp->dis_resfree)(res);

done:
	/*
	 * Free arguments struct
	 */
	if (!SVC_FREEARGS(xprt, disp->dis_xdrargs, args)) {
		cmn_err(CE_NOTE, "acl_server: bad freeargs");
		error++;
	}

	if (exi != NULL)
		export_rw_exit();

end:
	svstat_ptr[NFS_BADCALLS].value.ul += error;
	svstat_ptr[NFS_CALLS].value.ul++;
}

/*
 * Check to see if the given name corresponds to a
 * root user of the exported filesystem.
 * Used for AUTH_DES
 */
static int
rootname(struct export *ex, char *netname)
{
	int i;
	int namelen;

	namelen = strlen(netname) + 1;
	for (i = 0; i < ex->ex_des.nnames; i++) {
		if (bcmp(netname, ex->ex_des.rootnames[i], namelen) == 0) {
			return (1);
		}
	}
	return (0);
}

/*
 * Check to see if the given name corresponds to a
 * root user of the exported filesystem.
 * Kerberos principal name is "root"
 * Instance is the client's machine name.
 * Used for AUTH_KERB
 */
static int
kerbrootname(struct export *ex, struct authkerb_clnt_cred *akcc)
{
	int i;
	int namelen;

	if (bcmp(akcc->pname, "root", 5) != 0) {
		/* not a root user */
		goto notroot;
	}

	/* now check the instance for machine name match */
	namelen = strlen(akcc->pinst) + 1;
	for (i = 0; i < ex->ex_kerb.nnames; i++) {
		if (bcmp(akcc->pinst, ex->ex_kerb.rootnames[i], namelen) == 0) {
			return (1);
		}
	}

notroot:
	return (0);
}

/*
 * Fix for bug 1038302 - corbin
 * Added anon_ok argument.
 */
static int
checkauth(struct exportinfo *exi, struct svc_req *req, cred_t *cr, int anon_ok)
{
	struct authunix_parms *aup;
	struct authdes_cred *adc;
	struct authkerb_clnt_cred *akcc;
	int flavor;
	short grouplen;

	/*
	 *	Check for privileged port number
	 *	N.B.:  this assumes that we know the format of a netbuf.
	 */
	if (nfs_portmon) {
		struct sockaddr *ca;
		ca = (struct sockaddr *) svc_getrpccaller(req->rq_xprt)->buf;

		if (ca->sa_family == AF_INET &&
		    ntohs(((struct sockaddr_in *)ca)->sin_port) >=
		    IPPORT_RESERVED) {
			cmn_err(CE_NOTE, "NFS request from unprivileged port.");
			return (0);
		}
	}

	/*
	 * Set uid, gid, and gids to auth params
	 */
	flavor = req->rq_cred.oa_flavor;
	if (flavor != exi->exi_export.ex_auth) {
		flavor = AUTH_NULL;

#ifdef notdef_XXXX
		/*
		 * Fix for bug 1038302 - corbin
		 * only allow anon override if credentials are of the
		 * correct flavor.  XXX is this really necessary?
		 */
		anon_ok = 0;
#endif /* notdef_XXXX */
	}
	switch (flavor) {
	case AUTH_NULL:
		cr->cr_uid = exi->exi_export.ex_anon;
		cr->cr_gid = exi->exi_export.ex_anon;
		cr->cr_ngroups = 0;
		break;

	case AUTH_UNIX:
		aup = (struct authunix_parms *)req->rq_clntcred;
		if (aup->aup_uid == 0 &&
		    !hostinlist(svc_getrpccaller(req->rq_xprt),
				&exi->exi_export.ex_unix.rootaddrs)) {
			cr->cr_uid = exi->exi_export.ex_anon;
			cr->cr_gid = exi->exi_export.ex_anon;
			cr->cr_ngroups = 0;
		} else {
			cr->cr_uid = aup->aup_uid;
			cr->cr_gid = aup->aup_gid;
			bcopy((caddr_t)aup->aup_gids, (caddr_t)cr->cr_groups,
			    aup->aup_len * sizeof (cr->cr_groups[0]));
			cr->cr_ngroups = aup->aup_len;
		}
		break;


	case AUTH_DES:
		adc = (struct authdes_cred *)req->rq_clntcred;
		if (adc->adc_fullname.window > exi->exi_export.ex_des.window)
			return (0);
		if (!authdes_getucred(adc, &cr->cr_uid, &cr->cr_gid,
				    &grouplen, cr->cr_groups)) {
			if (rootname(&exi->exi_export, adc->adc_fullname.name))
				cr->cr_uid = 0;
			else
				cr->cr_uid = exi->exi_export.ex_anon;
			cr->cr_gid = exi->exi_export.ex_anon;
			grouplen = 0;
		} else if (cr->cr_uid == 0 && !rootname(&exi->exi_export,
						    adc->adc_fullname.name)) {
			cr->cr_uid = cr->cr_gid = exi->exi_export.ex_anon;
			grouplen = 0;
		}
		cr->cr_ngroups = grouplen;
		break;

	case AUTH_KERB:
		akcc = (struct authkerb_clnt_cred *)req->rq_clntcred;
		if (akcc->window > exi->exi_export.ex_kerb.window)
			return (0);
		/*
		 * XXX
		 * cr->cr_groups is a gid_t[], while the corresponding
		 * argument to authkerb_getucred is int[].  This works
		 * ok with present definition of gid_t, but might not
		 * work if it is changed.
		 */
		if (!authkerb_getucred(req, &cr->cr_uid,
		    &cr->cr_gid, &grouplen, (int *)cr->cr_groups)) {
			if (kerbrootname(&exi->exi_export, akcc))
				cr->cr_uid = 0;
			else
				cr->cr_uid = exi->exi_export.ex_anon;
			cr->cr_gid = exi->exi_export.ex_anon;
			grouplen = 0;
		} else if (cr->cr_uid == 0 && !kerbrootname(&exi->exi_export,
							    akcc)) {
			cr->cr_uid = cr->cr_gid = exi->exi_export.ex_anon;
			grouplen = 0;
		}
		cr->cr_ngroups = grouplen;
		break;

	default:
		return (0);
	}

	/*
	 * Even if anon access is disallowed via ex_anon==-1, we allow this
	 * access if anon_ok is set.  So set creds to the default "nobody"
	 * id.
	 */
	if (cr->cr_uid == (uid_t) -1) {
		cr->cr_uid = UID_NOBODY;
		cr->cr_gid = GID_NOBODY;
	} else {
		/* creds are valid even if anonymous, so set flag */
		anon_ok = 1;
	}

	/*
	 * Set real UID/GID to effective UID/GID
	 * corbin 6/19/90 - Fix bug 1029628
	 */
	cr->cr_ruid = cr->cr_uid;
	cr->cr_rgid = cr->cr_gid;

	return (anon_ok);
}

/*
 * NFS Server initialization routine.  This routine should only be called
 * once.  It performs the following tasks:
 *	- Call sub-initialization routines (localize access to variables)
 *	- Initialize all locks
 *	- initialize the version 3 write verifier
 */
int
nfs_srvinit(void)
{
	int error;

	error = nfs_exportinit();
	if (error != 0) {
		return (error);
	}

	rfs_srvrinit();
	rfs3_srvrinit();

	return (0);
}
