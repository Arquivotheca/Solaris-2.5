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
 *	(c) 1986,1987,1988,1989,1990,1991,1994  Sun Microsystems, Inc.
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

#ifndef	_NFS_EXPORT_H
#define	_NFS_EXPORT_H

#pragma ident	"@(#)export.h	1.22	95/01/13 SMI"
/*	export.h 1.7 88/08/19 SMI */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * exported vfs flags.
 */

#define	EX_RDONLY	0x01	/* exported read only */
#define	EX_RDMOSTLY	0x02	/* exported read mostly */
#define	EX_RDWR		0x04	/* exported read-write */
#define	EX_EXCEPTIONS	0x08	/* exported with ``exceptions'' lists */
#define	EX_NOSUID	0x10	/* exported with unsetable set[ug]ids */
#define	EX_ACLOK	0x20	/* exported with maximal access if acl exists */
#define	EX_ALL		(EX_RDONLY | EX_RDMOSTLY | EX_RDWR | \
					EX_EXCEPTIONS | EX_NOSUID | EX_ACLOK)

#define	EXMAXADDRS	256	/* max number in address list */
struct exaddrlist {
	unsigned naddrs;		/* number of addresses */
	struct netbuf *addrvec;		/* pointer to array of addresses */
	struct netbuf *addrmask;	/* mask of comparable bits of addrvec */
};

/*
 * Associated with AUTH_UNIX is an array of internet addresses
 * to check root permission.
 */
#define	EXMAXROOTADDRS	256		/* should be config option */
struct unixexport {
	struct exaddrlist rootaddrs;
};

/*
 * Associated with AUTH_DES is a list of network names to check
 * root permission, plus a time window to check for expired
 * credentials.
 */
#define	EXMAXROOTNAMES	256		/* should be config option */
struct desexport {
	unsigned nnames;
	char **rootnames;
	int window;
};

/*
 * Associated with AUTH_KERB is a list of network names to check
 * root permission, plus a time window to check for expired
 * credentials.
 * EXMAXROOTNAMES defined above applies to kerbexport as well.
 */
struct kerbexport {
	unsigned nnames;
	char **rootnames;
	int window;
};

/*
 * The export information passed to exportfs()
 */
struct export {
	int		ex_flags;	/* flags */
	unsigned	ex_anon;	/* uid for unauthenticated requests */
	int		ex_auth;	/* switch */
	union {
		struct unixexport	exunix;		/* case AUTH_UNIX */
		struct desexport	exdes;		/* case AUTH_DES */
		struct kerbexport	exkerb;		/* case AUTH_KERB */
	} ex_u;
	struct exaddrlist ex_roaddrs;
	struct exaddrlist ex_rwaddrs;
};
#define	ex_des	ex_u.exdes
#define	ex_unix	ex_u.exunix
#define	ex_kerb	ex_u.exkerb

#ifdef	_KERNEL
/*
 * A node associated with an export entry on the list of exported
 * filesystems.
 *
 * The exportinfo structure is protected by the exi_lock.  You must have
 * the writer lock to delete an exportinfo structure from the list.
 */

struct exportinfo {
	struct export		exi_export;
	fsid_t			exi_fsid;
	struct fid		exi_fid;
	struct exportinfo	*exi_hash;
	fhandle_t		exi_fh;
};

#define	EQFSID(fsidp1, fsidp2)	\
	(((fsidp1)->val[0] == (fsidp2)->val[0]) && \
	    ((fsidp1)->val[1] == (fsidp2)->val[1]))

#define	EQFID(fidp1, fidp2)	\
	((fidp1)->fid_len == (fidp2)->fid_len && \
	    nfs_fhbcmp((char *)(fidp1)->fid_data, (char *)(fidp2)->fid_data, \
	    (uint_t)(fidp1)->fid_len) == 0)

/*
 * Returns true iff exported filesystem is read-only to the given host.
 * This can happen in two ways:  first, if the default export mode is
 * read only, and the host's address isn't in the rw list;  second,
 * if the default export mode is read-write but the host's address
 * is in the ro list.  The optimization (checking for EX_EXCEPTIONS)
 * is to allow us to skip the calls to hostinlist if no host exception
 * lists were loaded.
 *
 * Note:  this macro should be as fast as possible since it's called
 * on each NFS modification request.
 */
#define	rdonly(exi, req)						\
	((((exi)->exi_export.ex_flags & EX_RDONLY) &&			\
	    ((!((exi)->exi_export.ex_flags & EX_EXCEPTIONS)) ||		\
		!hostinlist(svc_getrpccaller((req)->rq_xprt), 		\
		    &(exi)->exi_export.ex_rwaddrs))) ||			\
	(((exi)->exi_export.ex_flags & EX_RDWR) &&			\
	    (((exi)->exi_export.ex_flags & EX_EXCEPTIONS) &&		\
		hostinlist(svc_getrpccaller((req)->rq_xprt),		\
		    &(exi)->exi_export.ex_roaddrs))))

extern int	nfs_fhhash(fsid_t *, fid_t *);
extern int	nfs_fhbcmp(char *, char *, int);
extern int	nfs_exportinit(void);
extern int	makefh(fhandle_t *, struct vnode *, struct exportinfo *);
extern int	makefh3(nfs_fh3 *, struct vnode *, struct exportinfo *);
extern vnode_t *nfs_fhtovp(fhandle_t *, struct exportinfo *);
extern vnode_t *nfs3_fhtovp(nfs_fh3 *, struct exportinfo *);
extern struct	exportinfo *findexport(fsid_t *, struct fid *);
extern struct	exportinfo *checkexport(fsid_t *, struct fid *);
extern void	export_rw_exit(void);
extern int	hostinlist(struct netbuf *, struct exaddrlist *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _NFS_EXPORT_H */
