/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)lookup.c	1.19	94/08/29 SMI"

/*
 * This file contains the file lookup code for NFS.
 */

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/xdr.h>
#include <rpc/rpc_msg.h>
#include <sys/t_lock.h>
#include <rpc/clnt.h>
#include <nfs_prot.h>
#include <mount.h>
#include <pathname.h>
#include <sys/errno.h>
#include <sys/promif.h>
#include <local.h>
#include <sys/sainet.h>

/*
 * XXX	These should be in a header file!
 */
extern void nfs_error(enum nfsstat status);
extern enum clnt_stat rpc_call(u_long prog, u_long vers, u_long proc,
    xdrproc_t in_xdr, caddr_t args, xdrproc_t out_xdr, caddr_t ret,
    int rexmit, int wait_time, struct sainet *net, u_int auth);

extern struct nfs_file roothandle;	/* from nfs_mountroot() */

/*
 * starting at current directory (root for us), lookup the pathname.
 * return the file handle of said file.
 */

static int getsymlink(struct nfs_fh *fh, struct pathname *pnp);
static int lookuppn(struct pathname *pnp, struct nfs_file *cfile);

int
lookup(char *pathname, struct nfs_file *cur_file)
{
	struct pathname pnp;
	int error;

	static char lkup_path[NFS_MAXPATHLEN];	/* pn_alloc doesn't */

	pnp.pn_buf = &lkup_path[0];
	bzero(pnp.pn_buf, NFS_MAXPATHLEN);
	error = pn_get(pathname, &pnp);
	if (error)
		return (error);
	error = lookuppn(&pnp, cur_file);
	return (error);
}

static int
lookuppn(struct pathname *pnp, struct nfs_file *cfile)
{
	enum clnt_stat status;
	char component[NFS_MAXNAMLEN+1];	/* buffer for component */
	int nlink = 0;
	int lookup_flags;
	diropargs dirop;
	diropargs *diropp = &dirop;
	int error = 0;
	diropres res_lookup;

	*cfile = roothandle;	/* structure copy - start at the root. */
begin:
	/*
	 * Each time we begin a new name interpretation (e.g.
	 * when first called and after each symbolic link is
	 * substituted), we allow the search to start at the
	 * root directory if the name starts with a '/', otherwise
	 * continuing from the current directory.
	 */
	component[0] = '\0';
	if (pn_peekchar(pnp) == '/') {
		*cfile = roothandle;
		pn_skipslash(pnp);
	}

next:
	/*
	 * Make sure we have a directory.
	 */
	if (cfile->type != NFDIR) {
		error = ENOTDIR;
		goto bad;
	}
	/*
	 * Process the next component of the pathname.
	 */
	error = pn_stripcomponent(pnp, component);
	if (error)
		goto bad;

	/*
	 * Check for degenerate name (e.g. / or "")
	 * which is a way of talking about a directory,
	 * e.g. "/." or ".".
	 */
	if (component[0] == '\0') {
		return (0);
	}

	/*
	 * Handle "..": two special cases.
	 * 1. If at root directory (e.g. after chroot)
	 *    then ignore it so can't get out.
	 * 2. If this vnode is the root of a mounted
	 *    file system, then replace it with the
	 *    vnode which was mounted on so we take the
	 *    .. in the other file system.
	 */
	if (strcmp(component, "..") == 0) {
		if (cfile == &roothandle)
			goto skip;
	}

	/*
	 * Perform a lookup in the current directory.
	 */
	bcopy((caddr_t)&cfile->fh, (caddr_t)&diropp->dir, FHSIZE);
	diropp->name = component;
	status = rpc_call((u_long)NFS_PROGRAM, (u_long)NFS_VERSION,
	    (u_long)NFSPROC_LOOKUP, xdr_diropargs, (caddr_t)diropp,
	    xdr_diropres, (caddr_t)&res_lookup, 0, 0, 0, AUTH_UNIX);
	if (status != RPC_SUCCESS) {
		printf("lookup: RPC error.\n");
		return (-1);
	}
	if (res_lookup.status != NFS_OK) {
		nfs_error(res_lookup.status);
		return (res_lookup.status);
	}

	/*
	 * If we hit a symbolic link and there is more path to be
	 * translated or this operation does not wish to apply
	 * to a link, then place the contents of the link at the
	 * front of the remaining pathname.
	 */
	if (res_lookup.diropres_u.diropres.attributes.type == NFLNK) {

		struct pathname linkpath;
		static char path_tmp[NFS_MAXPATHLEN];	/* used for symlinks */

		linkpath.pn_buf = &path_tmp[0];

		nlink++;
		if (nlink > MAXSYMLINKS) {
			error = ELOOP;
			goto bad;
		}
		error = getsymlink(&res_lookup.diropres_u.diropres.file,
		    &linkpath);
		if (error)
			goto bad;
		if (pn_pathleft(&linkpath) == 0)
			(void) pn_set(&linkpath, ".");
		error = pn_combine(pnp, &linkpath);	/* linkpath before pn */
		if (error)
			goto bad;
		goto begin;
	}

	bcopy((caddr_t)&res_lookup.diropres_u.diropres.file,
	    (caddr_t)&cfile->fh, FHSIZE);
	cfile->type = (int)res_lookup.diropres_u.diropres.attributes.type;
	cfile->status = res_lookup.status;

skip:
	/*
	 * Skip to next component of the pathname.
	 * If no more components, return last directory (if wanted)  and
	 * last component (if wanted).
	 */
	if (pn_pathleft(pnp) == 0) {
		(void) pn_set(pnp, component);
		return (0);
	}
	/*
	 * skip over slashes from end of last component
	 */
	pn_skipslash(pnp);

	goto next;
bad:
	/*
	 * Error.
	 */
	return (error);

}

/*
 * Gets symbolic link into pathname.
 */
static int
getsymlink(struct nfs_fh *fh, struct pathname *pnp)
{
	enum clnt_stat status;
	struct readlinkres linkres;

	static char symlink_path[NFS_MAXPATHLEN];

	/*
	 * linkres needs a zeroed buffer to place path data into:
	 */
	bzero(symlink_path, NFS_MAXPATHLEN);
	linkres.readlinkres_u.data = &symlink_path[0];

	status = rpc_call((u_long)NFS_PROGRAM, (u_long)NFS_VERSION,
	    (u_long)NFSPROC_READLINK, xdr_nfs_fh, (caddr_t)fh,
	    xdr_readlinkres, (caddr_t)&linkres, 0, 0, 0, AUTH_UNIX);
	if (status != RPC_SUCCESS) {
		printf("getsymlink: RPC call failed.\n");
		return (-1);
	}
	if (linkres.status != NFS_OK) {
		nfs_error(linkres.status);
		return (linkres.status);
	}

	pn_get(linkres.readlinkres_u.data, pnp);

	return (NFS_OK);
}
