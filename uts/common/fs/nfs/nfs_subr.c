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
 *  	Copyright (c) 1986-1989,1994,1995 by Sun Microsystems, Inc.
 *  	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 */

#pragma ident	"@(#)nfs_subr.c	1.112	95/08/09 SMI"	/* SVr4.0 1.14	*/

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/tiuser.h>
#include <sys/swap.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/cmn_err.h>
#include <sys/vtrace.h>
#include <sys/session.h>
#include <sys/dnlc.h>
#include <sys/bitmap.h>
#include <sys/acl.h>
#include <sys/ddi.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <nfs/nfs_acl.h>

#include <vm/pvn.h>

#include <kerberos/krb.h>

/*
 * Mutex to protect the following variables:
 *	rtable
 *	r_hash	(in rnode)
 *	rtablecnt
 *	rpfreelist
 *	r_freef	(in rnode)
 *	r_freeb	(in rnode)
 *	rnew
 */
kmutex_t nfs_rtable_lock;

static rnode_t **rtable;
#ifdef DEBUG
static int *rtablecnt;
#endif
static rnode_t *rpfreelist = NULL;
static int rnew = 0;
static int nrnode = 0;
static int rtablesize;
static int rtablemask;
static int hashlen = 4;
static struct kmem_cache *rnode_cache;

/*
 * Mutex to protect the following variables:
 *	nfs_major
 *	nfs_minor
 */
kmutex_t nfs_minor_lock;
int nfs_major;
int nfs_minor;

/*
 * Client side utilities
 */

/*
 * client side statistics
 */
static struct {
	kstat_named_t	calls;			/* client requests */
	kstat_named_t	badcalls;		/* rpc failures */
	kstat_named_t	clgets;			/* client handle gets */
	kstat_named_t	cltoomany;		/* Client handle cache misses */
#ifdef DEBUG
	kstat_named_t	nrnode;			/* number of allocated rnodes */
	kstat_named_t	access;			/* size of access cache */
	kstat_named_t	dirent;			/* size of readdir cache */
	kstat_named_t	symlink;		/* size of symlink cache */
	kstat_named_t	reclaim;		/* number of reclaims */
	kstat_named_t	f_reclaim;		/* number of free reclaims */
	kstat_named_t	a_reclaim;		/* number of active reclaims */
	kstat_named_t	r_reclaim;		/* number of rnode reclaims */
#endif
} clstat = {
	{ "calls",	KSTAT_DATA_ULONG },
	{ "badcalls",	KSTAT_DATA_ULONG },
	{ "clgets",	KSTAT_DATA_ULONG },
	{ "cltoomany",	KSTAT_DATA_ULONG },
#ifdef DEBUG
	{ "nrnode",	KSTAT_DATA_ULONG },
	{ "access",	KSTAT_DATA_ULONG },
	{ "dirent",	KSTAT_DATA_ULONG },
	{ "symlink",	KSTAT_DATA_ULONG },
	{ "reclaim",	KSTAT_DATA_ULONG },
	{ "f_reclaim",	KSTAT_DATA_ULONG },
	{ "a_reclaim",	KSTAT_DATA_ULONG },
	{ "r_reclaim",	KSTAT_DATA_ULONG },
#endif
};

kstat_named_t *clstat_ptr = (kstat_named_t *) &clstat;
ulong_t	clstat_ndata = sizeof (clstat) / sizeof (kstat_named_t);

#ifdef DEBUG
static kmutex_t nfs_accurate_stats;
#endif

#define	MAXCLIENTS	16

struct chtab {
	uint ch_timesused;
	bool_t ch_inuse;
	u_long ch_prog;
	u_long ch_vers;
	dev_t ch_dev;
	CLIENT *ch_client;
	struct chtab *ch_list;
	struct chtab *ch_next;
};

static struct chtab *chtable = NULL;

/*
 * chtable_lock protects chtable[].
 */
static kmutex_t chtable_lock;

static u_int authdes_win = 5 * 60; /* five minutes -- should be mount option */

static struct desauthent {
	mntinfo_t *da_mi;
	uid_t da_uid;
	short da_inuse;
	AUTH *da_auth;
} desauthtab[MAXCLIENTS];
static int nextdesvictim;
static kmutex_t desauthtab_lock;	/* Lock to protect DES auth cache */

static u_int authkerb_win = 5 * 60; /* five minutes -- should be mount option */

static struct kerbauthent {
	mntinfo_t *ka_mi;
	uid_t ka_uid;
	short ka_inuse;
	AUTH *ka_auth;
} kerbauthtab[MAXCLIENTS];
static int nextkerbvictim;
static kmutex_t kerbauthtab_lock;	/* Lock to protect KERB auth cache */

struct unixauthent {
	struct unixauthent *ua_next;
	AUTH *ua_auth;
};
/* list of avail UNIX auth handles */
static struct unixauthent *unixauthtab = NULL;
/* list of all UNIX auth handles */
static struct unixauthent *unixauthlist;
static kmutex_t unixauthtab_lock; 	/* Lock to protect UNIX auth cache */

static long	authget(mntinfo_t *, cred_t *, AUTH **);
static void	kerb_create_failure(int, int);
static void	authfree(AUTH *);
static long	clget(mntinfo_t *, cred_t *, CLIENT **, struct chtab **);
static long	acl_clget(mntinfo_t *, cred_t *, CLIENT **, struct chtab **);
static void	clfree(CLIENT *, struct chtab *);
static int	nfs_feedback(int, int, mntinfo_t *);
static int	rfscall(mntinfo_t *, int, xdrproc_t, caddr_t,
			xdrproc_t, caddr_t, cred_t *, int *, enum clnt_stat *);
static int	aclcall(mntinfo_t *, int, xdrproc_t, caddr_t,
			xdrproc_t, caddr_t, cred_t *, int *);
static void	rinactive(rnode_t *, cred_t *);
static int	rtablehash(nfs_fhandle *);
static vnode_t	*make_rnode(nfs_fhandle *, struct vfs *, struct vnodeops *,
			int (*)(vnode_t *, page_t *, u_int *, u_int *, int,
			cred_t *), int *, cred_t *);
static void	rp_rmfree(rnode_t *);
static rnode_t	*rfind(nfs_fhandle *, struct vfs *);
static int	nfs_free_data_reclaim(rnode_t *);
static int	nfs_active_data_reclaim(rnode_t *);
static int	nfs_free_reclaim(void);
static int	nfs_active_reclaim(void);
static int	nfs_rnode_reclaim(void);
static void	nfs_reclaim(void);

static long
authget(mntinfo_t *mi, cred_t *cr, AUTH **ap)
{
	int i;
	AUTH *auth;
	register struct unixauthent *ua;
	register struct desauthent *da;
	int authflavor;
	cred_t *savecred;
	int stat;			/* return (errno) status */
	register char *p;
	register struct kerbauthent *ka;
	char kname[ANAME_SZ + INST_SZ + 1];
	int kstat;

	if (ap == NULL)
		return (EINVAL);
	*ap = (AUTH *)NULL;

	authflavor = mi->mi_authflavor;
	for (;;) {
		switch (authflavor) {
		case AUTH_NONE:
			/*
			 * XXX: should do real AUTH_NONE, instead of AUTH_UNIX
			 */
		case AUTH_UNIX:
			mutex_enter(&unixauthtab_lock);
			if ((ua = unixauthtab) != NULL)
				unixauthtab = ua->ua_next;
			mutex_exit(&unixauthtab_lock);

			if (ua == NULL) {
				/* overflow of unix auths */
				*ap = authkern_create();
				return ((*ap != NULL) ? 0 : EINTR);
			}

			if (ua->ua_auth == NULL)
				ua->ua_auth = authkern_create();
			*ap = ua->ua_auth;
			return ((*ap != NULL) ? 0 : EINTR);

		case AUTH_DES:
			mutex_enter(&desauthtab_lock);
			for (da = desauthtab;
			    da < &desauthtab[MAXCLIENTS];
			    da++) {
				if (da->da_mi == mi &&
				    da->da_uid == cr->cr_uid &&
				    !da->da_inuse &&
				    da->da_auth != NULL) {
					da->da_inuse = 1;
					mutex_exit(&desauthtab_lock);
					*ap = da->da_auth;
					return (0);
				}
			}
			mutex_exit(&desauthtab_lock);

			/*
			 *  A better way would be to have a cred paramater to
			 *  authdes_create.
			 */
			savecred = curthread->t_cred;
			curthread->t_cred = cr;
			stat = authdes_create(mi->mi_netname, authdes_win,
				&mi->mi_syncaddr, mi->mi_knetconfig,
				(des_block *)NULL,
				(mi->mi_flags & MI_RPCTIMESYNC) ? 1 : 0, &auth);
			curthread->t_cred = savecred;
			*ap = auth;

			if (stat != 0) {
				nfs_cmn_err(stat, CE_WARN,
					"authget: authdes_create failed: %m\n");
				/*
				 *  If MI_TRYANON flag is on, we will try again
				 *  with AUTH_NONE flavor.  See bug 1180236.
				 */
				if (mi->mi_flags & MI_TRYANON) {
					authflavor = AUTH_NONE;
					continue;
				} else
					return ((long)stat);
			}

			i = MAXCLIENTS;
			mutex_enter(&desauthtab_lock);
			do {
				da = &desauthtab[nextdesvictim++];
				nextdesvictim %= MAXCLIENTS;
			} while (da->da_inuse && --i > 0);

			if (da->da_inuse) {
				mutex_exit(&desauthtab_lock);
				/* overflow of des auths */
				return ((long)stat);
			}
			da->da_inuse = 1;
			mutex_exit(&desauthtab_lock);

			if (da->da_auth != NULL)
				auth_destroy(da->da_auth);

			da->da_auth = auth;
			da->da_uid = cr->cr_uid;
			da->da_mi = mi;
			return ((long)stat);

		case AUTH_KERB:
			mutex_enter(&kerbauthtab_lock);
			for (ka = kerbauthtab;
			    ka < &kerbauthtab[MAXCLIENTS];
			    ka++) {
				if (ka->ka_mi == mi &&
				    ka->ka_uid == cr->cr_uid &&
				    !ka->ka_inuse &&
				    ka->ka_auth != NULL) {
					ka->ka_inuse = 1;
					mutex_exit(&kerbauthtab_lock);
					*ap = ka->ka_auth;
					return (0);
				}
			}
			mutex_exit(&kerbauthtab_lock);

			/* separate principal name and instance */
			(void) strncpy(kname, mi->mi_netname,
					ANAME_SZ + INST_SZ);
			kname[ANAME_SZ + INST_SZ] = '\0';
			for (p = kname; *p && *p != '.'; p++)
				;
			if (*p)
				*p++ = '\0';

			/*
			 *  A better way would be to have a cred paramater to
			 *  authkerb_create.
			 */
			savecred = curthread->t_cred;
			curthread->t_cred = cr;
			stat = authkerb_create(kname, p, NULL, authkerb_win,
				&mi->mi_syncaddr, &kstat, mi->mi_knetconfig,
				(mi->mi_flags & MI_RPCTIMESYNC) ? 1 : 0, &auth);
			curthread->t_cred = savecred;
			*ap = auth;

			if (stat != 0) {
				kerb_create_failure(stat, kstat);
				/*
				 *  If MI_TRYANON flag is on, we will try again
				 *  with AUTH_NONE flavor.  See bug 1180236.
				 */
				if (mi->mi_flags & MI_TRYANON) {
					authflavor = AUTH_NONE;
					continue;
				} else
					return ((long)stat);
			}

			i = MAXCLIENTS;
			mutex_enter(&kerbauthtab_lock);
			do {
				ka = &kerbauthtab[nextkerbvictim++];
				nextkerbvictim %= MAXCLIENTS;
			} while (ka->ka_inuse && --i > 0);

			if (ka->ka_inuse) {
				mutex_exit(&kerbauthtab_lock);
				/* overflow of kerb auths */
				return ((long)stat);
			}
			ka->ka_inuse = 1;
			mutex_exit(&kerbauthtab_lock);

			if (ka->ka_auth != NULL)
				auth_destroy(ka->ka_auth);

			ka->ka_auth = auth;
			ka->ka_uid = cr->cr_uid;
			ka->ka_mi = mi;
			return ((long)stat);

		default:
			/*
			 * auth create must have failed, try AUTH_NONE
			 * (this relies on AUTH_NONE never failing)
			 */
			cmn_err(CE_WARN,
				"authget: unknown authflavor %d\n", authflavor);
			authflavor = AUTH_NONE;
		}
	}
}

/*
 * Print an error message about authkerb_create's failing.
 */

static void
kerb_create_failure(int stat, int kstat)
{
	char *kerbmsg;

	/*
	 * A kstat of 0 isn't an error, so don't assume it has a useful
	 * error message.
	 */
	if (kstat < 1 || kstat > MAX_KRB_ERRORS) {
		kerbmsg = NULL;
	} else {
		kerbmsg = krb_err_txt[kstat];
	}

	if (kerbmsg != NULL) {
		nfs_cmn_err(stat, CE_WARN,
			    "authget: authkerb_create failed: %m (%s)\n",
			    kerbmsg);
	} else {
		nfs_cmn_err(stat, CE_WARN,
		"authget: authkerb_create failed: %m (kerberos error %d)\n",
			    kstat);
	}
}

static void
authfree(AUTH *auth)
{
	register struct unixauthent *ua;
	register struct desauthent *da;
	register struct kerbauthent *ka;

	switch (auth->ah_cred.oa_flavor) {
	case AUTH_NONE: /* XXX: do real AUTH_NONE */
	case AUTH_UNIX:
		for (ua = unixauthlist; ua != NULL; ua = ua->ua_next) {
			if (ua->ua_auth == auth) {
				mutex_enter(&unixauthtab_lock);
				ua->ua_next = unixauthtab;
				unixauthtab = ua;
				mutex_exit(&unixauthtab_lock);
				return;
			}
		}
		auth_destroy(auth);	/* was overflow */
		break;
	case AUTH_DES:
		mutex_enter(&desauthtab_lock);
		for (da = desauthtab; da < &desauthtab[MAXCLIENTS]; da++) {
			if (da->da_auth == auth) {
				da->da_inuse = 0;
				mutex_exit(&desauthtab_lock);
				return;
			}
		}
		mutex_exit(&desauthtab_lock);
		auth_destroy(auth);	/* was overflow */
		break;
	case AUTH_KERB:
		mutex_enter(&kerbauthtab_lock);
		for (ka = kerbauthtab; ka < &kerbauthtab[MAXCLIENTS]; ka++) {
			if (ka->ka_auth == auth) {
				ka->ka_inuse = 0;
				mutex_exit(&kerbauthtab_lock);
				return;
			}
		}
		mutex_exit(&kerbauthtab_lock);
		auth_destroy(auth);	/* was overflow */
		break;
	default:
		cmn_err(CE_WARN, "authfree: unknown authflavor %d",
			auth->ah_cred.oa_flavor);
		break;
	}
}

/*
 *  Revoke all rpc credentials (of the selected auth type) for the given uid
 *  from the auth cache.  Must be root to do this if the requested uid is not
 *  the effective uid of the requestor.
 *
 *  Called from nfssys()
 *  Returns error number
 */
int
nfs_revoke_auth(int authtype, uid_t uid, cred_t *cr)
{
	register struct desauthent *da;
	register struct kerbauthent *ka;
	int error = 0;

	if (uid != cr->cr_uid && !suser(cr))
		return (EPERM);

	switch (authtype) {
	    case AUTH_DES:
		mutex_enter(&desauthtab_lock);
		for (da = desauthtab; da < &desauthtab[MAXCLIENTS]; da++) {
			if (uid == da->da_uid)
				rpc_revoke_key(da->da_auth, 1);
		}
		mutex_exit(&desauthtab_lock);
		break;

	    case AUTH_KERB:
		mutex_enter(&kerbauthtab_lock);
		for (ka = kerbauthtab; ka < &kerbauthtab[MAXCLIENTS]; ka++) {
			if (uid == ka->ka_uid)
				rpc_revoke_key(ka->ka_auth, 0);
		}
		mutex_exit(&kerbauthtab_lock);
		break;

	    default:
		/* not an auth type with cached creds */
		error = EINVAL;
		break;
	}

	return (error);
}

static long
clget(mntinfo_t *mi, cred_t *cr, CLIENT **newcl, struct chtab **chp)
{
	register struct chtab *ch;
	int retrans;
	CLIENT *client;
	register int error;
	int nhl;
	struct chtab **plistp;
	int readsize;

	if (newcl == NULL || chp == NULL)
		return (EINVAL);
	*newcl = NULL;
	*chp = NULL;


	/*
	 * Set read buffer size to rsize
	 * and add room for RPC headers.
	 */
	readsize = mi->mi_tsize;
	if (readsize != 0) {
		readsize += (RPC_MAXDATASIZE - NFS_MAXDATA);
	}

	/*
	 * If soft mount and server is down just try once.
	 * meaning: do not retransmit.
	 */
	if (!(mi->mi_flags & MI_HARD) && (mi->mi_flags & MI_DOWN))
		retrans = 0;
	else
		retrans = mi->mi_retrans;

	/*
	 * Find an unused handle or create one
	 */
	clstat.clgets.value.ul++;
	mutex_enter(&chtable_lock);
	plistp = &chtable;
	for (ch = chtable; ch != NULL; ch = ch->ch_next) {
		if (ch->ch_prog == mi->mi_prog &&
		    ch->ch_vers == mi->mi_vers &&
		    ch->ch_dev == mi->mi_knetconfig->knc_rdev)
			break;
		plistp = &ch->ch_next;
	}
	if (ch != NULL) {
		for (nhl = 1; ch != NULL; ch = ch->ch_list, nhl++) {
			if (!ch->ch_inuse) {
				ch->ch_inuse = TRUE;
				mutex_exit(&chtable_lock);
				if (ch->ch_client == NULL) {

					error =
					    clnt_tli_kcreate(mi->mi_knetconfig,
						&mi->mi_addr, mi->mi_prog,
						mi->mi_vers, readsize,
						retrans, cr, &ch->ch_client);
					if (error != 0) {
						ch->ch_inuse = FALSE;
						nfs_cmn_err(error, CE_WARN,
				"clget: null client in chtable, ch=%x: %m\n",
							    (int)ch);
						return (error);
					}
					auth_destroy(ch->ch_client->cl_auth);
				} else {
					clnt_tli_kinit(ch->ch_client,
						mi->mi_knetconfig,
						&mi->mi_addr, readsize,
						retrans, cr);
				}
				error = authget(mi, cr,
						&ch->ch_client->cl_auth);
				if (error || ch->ch_client->cl_auth == NULL) {
					CLNT_DESTROY(ch->ch_client);
					ch->ch_client = NULL;
					ch->ch_inuse = FALSE;
					nfs_cmn_err(error, CE_WARN,
			    "clget: authget failed (scanning chtable): %m\n");
					return ((error != 0) ? error : EINTR);
				}
				ch->ch_timesused++;
				*newcl = ch->ch_client;
				*chp = ch;
				return (0);
			}
			plistp = &ch->ch_list;
		}
		if (nhl == MAXCLIENTS) {
			mutex_exit(&chtable_lock);
			goto toomany;
		}
	}
	ch = (struct chtab *)kmem_alloc(sizeof (*ch), KM_SLEEP);
	ch->ch_timesused = 0;
	ch->ch_inuse = TRUE;
	ch->ch_prog = mi->mi_prog;
	ch->ch_vers = mi->mi_vers;
	ch->ch_dev = mi->mi_knetconfig->knc_rdev;
	ch->ch_client = NULL;
	ch->ch_list = NULL;
	ch->ch_next = NULL;
	*plistp = ch;
	mutex_exit(&chtable_lock);

	error = clnt_tli_kcreate(mi->mi_knetconfig, &mi->mi_addr, mi->mi_prog,
			mi->mi_vers, readsize, retrans, cr, &ch->ch_client);
	if (error != 0) {
		ch->ch_inuse = FALSE;
		/*
		 * Warning is unnecessary if error is EINTR.
		 */
		if (error != EINTR)
			nfs_cmn_err(error, CE_WARN,
			    "clget: couldn't create handle: %m\n");
		return (error);
	}
	auth_destroy(ch->ch_client->cl_auth);
	error = authget(mi, cr, &ch->ch_client->cl_auth);
	if (error || ch->ch_client->cl_auth == NULL) {
		CLNT_DESTROY(ch->ch_client);
		ch->ch_client = NULL;
		ch->ch_inuse = FALSE;
		nfs_cmn_err(error, CE_WARN, "clget: authget failed: %m\n");
		return ((error != 0) ? error : EINTR);
	}
	ch->ch_timesused++;
	*newcl = ch->ch_client;
	*chp = ch;
	return (0);

	/*
	 * If we got here there are no available handles
	 * To avoid deadlock, don't wait, but just grab another
	 */
toomany:
	clstat.cltoomany.value.ul++;
	error = clnt_tli_kcreate(mi->mi_knetconfig, &mi->mi_addr, mi->mi_prog,
			mi->mi_vers, readsize, retrans, cr, &client);
	if (error != 0) {
		/*
		 * Warning is unnecessary if error is EINTR.
		 */
		if (error != EINTR)
			nfs_cmn_err(error, CE_WARN,
			    "clget: couldn't create handle: %m\n");
		return (error);
	}
	auth_destroy(client->cl_auth);	 /* XXX */
	error = authget(mi, cr, &client->cl_auth);
	if (error || client->cl_auth == NULL) {
		nfs_cmn_err(error, CE_WARN, "clget: authget failed: %m\n");
		CLNT_DESTROY(client);
		return ((error != 0) ? error : EINTR);
	}
	*newcl = client;
	return (0);
}

static long
acl_clget(mntinfo_t *mi, cred_t *cr, CLIENT **newcl, struct chtab **chp)
{
	register struct chtab *ch;
	int retrans;
	CLIENT *client;
	register int error;
	int nhl;
	struct chtab **plistp;
	int readsize;

	if (newcl == NULL || chp == NULL)
		return (EINVAL);
	*newcl = NULL;
	*chp = NULL;


	/*
	 * Set read buffer size to rsize
	 * and add room for RPC headers.
	 */
	readsize = mi->mi_tsize;
	if (readsize != 0) {
		readsize += (RPC_MAXDATASIZE - NFS_MAXDATA);
	}

	/*
	 * If soft mount and server is down just try once.
	 * meaning: do not retransmit.
	 */
	if (!(mi->mi_flags & MI_HARD) && (mi->mi_flags & MI_DOWN))
		retrans = 0;
	else
		retrans = mi->mi_retrans;

	/*
	 * Find an unused handle or create one
	 */
	clstat.clgets.value.ul++;
	mutex_enter(&chtable_lock);
	plistp = &chtable;
	for (ch = chtable; ch != NULL; ch = ch->ch_next) {
		if (ch->ch_prog == NFS_ACL_PROGRAM &&
		    ch->ch_vers == mi->mi_vers &&
		    ch->ch_dev == mi->mi_knetconfig->knc_rdev)
			break;
		plistp = &ch->ch_next;
	}
	if (ch != NULL) {
		for (nhl = 1; ch != NULL; ch = ch->ch_list, nhl++) {
			if (!ch->ch_inuse) {
				ch->ch_inuse = TRUE;
				mutex_exit(&chtable_lock);
				if (ch->ch_client == NULL) {

					error =
					    clnt_tli_kcreate(mi->mi_knetconfig,
						&mi->mi_addr, NFS_ACL_PROGRAM,
						mi->mi_vers, readsize,
						retrans, cr, &ch->ch_client);
					if (error != 0) {
						ch->ch_inuse = FALSE;
						nfs_cmn_err(error, CE_WARN,
				"clget: null client in chtable, ch=%x: %m\n",
							    (int)ch);
						return (error);
					}
					auth_destroy(ch->ch_client->cl_auth);
				} else {
					clnt_tli_kinit(ch->ch_client,
						mi->mi_knetconfig,
						&mi->mi_addr, readsize,
						retrans, cr);
				}
				error = authget(mi, cr,
						&ch->ch_client->cl_auth);
				if (error || ch->ch_client->cl_auth == NULL) {
					CLNT_DESTROY(ch->ch_client);
					ch->ch_client = NULL;
					ch->ch_inuse = FALSE;
					nfs_cmn_err(error, CE_WARN,
			    "clget: authget failed (scanning chtable): %m\n");
					return ((error != 0) ? error : EINTR);
				}
				ch->ch_timesused++;
				*newcl = ch->ch_client;
				*chp = ch;
				return (0);
			}
			plistp = &ch->ch_list;
		}
		if (nhl == MAXCLIENTS) {
			mutex_exit(&chtable_lock);
			goto toomany;
		}
	}
	ch = (struct chtab *)kmem_alloc(sizeof (*ch), KM_SLEEP);
	ch->ch_timesused = 0;
	ch->ch_inuse = TRUE;
	ch->ch_prog = NFS_ACL_PROGRAM;
	ch->ch_vers = mi->mi_vers;
	ch->ch_dev = mi->mi_knetconfig->knc_rdev;
	ch->ch_client = NULL;
	ch->ch_list = NULL;
	ch->ch_next = NULL;
	*plistp = ch;
	mutex_exit(&chtable_lock);

	error = clnt_tli_kcreate(mi->mi_knetconfig, &mi->mi_addr,
				NFS_ACL_PROGRAM, mi->mi_vers, readsize,
				retrans, cr, &ch->ch_client);
	if (error != 0) {
		ch->ch_inuse = FALSE;
		/*
		 * Warning is unnecessary if error is EINTR.
		 */
		if (error != EINTR)
			nfs_cmn_err(error, CE_WARN,
			    "clget: couldn't create handle: %m\n");
		return (error);
	}
	auth_destroy(ch->ch_client->cl_auth);
	error = authget(mi, cr, &ch->ch_client->cl_auth);
	if (error || ch->ch_client->cl_auth == NULL) {
		CLNT_DESTROY(ch->ch_client);
		ch->ch_client = NULL;
		ch->ch_inuse = FALSE;
		nfs_cmn_err(error, CE_WARN, "clget: authget failed: %m\n");
		return ((error != 0) ? error : EINTR);
	}
	ch->ch_timesused++;
	*newcl = ch->ch_client;
	*chp = ch;
	return (0);

	/*
	 * If we got here there are no available handles
	 * To avoid deadlock, don't wait, but just grab another
	 */
toomany:
	clstat.cltoomany.value.ul++;
	error = clnt_tli_kcreate(mi->mi_knetconfig, &mi->mi_addr,
				NFS_ACL_PROGRAM, mi->mi_vers, readsize,
				retrans, cr, &client);
	if (error != 0) {
		/*
		 * Warning is unnecessary if error is EINTR.
		 */
		if (error != EINTR)
			nfs_cmn_err(error, CE_WARN,
			    "clget: couldn't create handle: %m\n");
		return (error);
	}
	auth_destroy(client->cl_auth);	 /* XXX */
	error = authget(mi, cr, &client->cl_auth);
	if (error || client->cl_auth == NULL) {
		nfs_cmn_err(error, CE_WARN, "clget: authget failed: %m\n");
		CLNT_DESTROY(client);
		return ((error != 0) ? error : EINTR);
	}
	*newcl = client;
	return (0);
}

static void
clfree(CLIENT *cl, struct chtab *ch)
{

	authfree(cl->cl_auth);
	cl->cl_auth = NULL;

	if (ch != NULL) {
		ch->ch_inuse = FALSE;
		return;
	}

	/* destroy any extra allocated above MAXCLIENTS */
	CLNT_DESTROY(cl);
}

/*
 * Minimum time-out values indexed by call type
 * These units are in "eights" of a second to avoid multiplies
 */
static unsigned int minimum_timeo[] = {
	6, 7, 10 };

/*
 * Back off for retransmission timeout, MAXTIMO is in hz of a sec
 */
#define	MAXTIMO	(20*HZ)
#define	backoff(tim)	((((tim) << 1) > MAXTIMO) ? MAXTIMO : ((tim) << 1))

#define	MIN_NFS_TSIZE 512	/* minimum "chunk" of NFS IO */
#define	REDUCE_NFS_TIME (HZ/2)	/* rtxcur we try to keep under */
#define	INCREASE_NFS_TIME (HZ/3*8) /* srtt we try to keep under (scaled*8) */

/*
 * Function called when rfscall notices that we have been
 * re-transmitting, or when we get a response without retransmissions.
 * Return 1 if the transfer size was adjusted down - 0 if no change.
 */
static int
nfs_feedback(int flag, int which, mntinfo_t *mi)
{
	int kind;
	int r = 0;

	mutex_enter(&mi->mi_lock);
	if (flag == FEEDBACK_REXMIT1) {
		if (mi->mi_timers[NFS_CALLTYPES].rt_rtxcur != 0 &&
		    mi->mi_timers[NFS_CALLTYPES].rt_rtxcur < REDUCE_NFS_TIME)
			goto done;
		if (mi->mi_curread > MIN_NFS_TSIZE) {
			mi->mi_curread /= 2;
			if (mi->mi_curread < MIN_NFS_TSIZE)
				mi->mi_curread = MIN_NFS_TSIZE;
			r = 1;
		}

		if (mi->mi_curwrite > MIN_NFS_TSIZE) {
			mi->mi_curwrite /= 2;
			if (mi->mi_curwrite < MIN_NFS_TSIZE)
				mi->mi_curwrite = MIN_NFS_TSIZE;
			r = 1;
		}
	} else if (flag == FEEDBACK_OK) {
		kind = mi->mi_timer_type[which];
		if (kind == 0 ||
		    mi->mi_timers[kind].rt_srtt >= (u_short) INCREASE_NFS_TIME)
			goto done;
		if (kind == 1) {
			if (mi->mi_curread >= mi->mi_tsize)
				goto done;
			mi->mi_curread +=  MIN_NFS_TSIZE;
			if (mi->mi_curread > mi->mi_tsize/2)
				mi->mi_curread = mi->mi_tsize;
		} else if (kind == 2) {
			if (mi->mi_curwrite >= mi->mi_stsize)
				goto done;
			mi->mi_curwrite += MIN_NFS_TSIZE;
			if (mi->mi_curwrite > mi->mi_stsize/2)
				mi->mi_curwrite = mi->mi_stsize;
		}
	}
done:
	mutex_exit(&mi->mi_lock);
	return (r);
}

#ifdef DEBUG
static int rfs2call_hits = 0;
static int rfs2call_misses = 0;
#endif

int
rfs2call(mntinfo_t *mi, int which, xdrproc_t xdrargs, caddr_t argsp,
	xdrproc_t xdrres, caddr_t resp, cred_t *cr, int *douprintf,
	enum nfsstat *statusp)
{
	int rpcerror;
	enum clnt_stat rpc_status;

	ASSERT(statusp != NULL);

	rpcerror = rfscall(mi, which, xdrargs, argsp, xdrres, resp,
			    cr, douprintf, &rpc_status);
	if (!rpcerror) {
		/*
		 * Boy is this a kludge!  If the reply status is
		 * NFSERR_EACCES, it may be because we are root
		 * (no root net access).  Check the real uid, if
		 * it isn't root make that the uid instead and
		 * retry the call.
		 */
		if (*statusp == NFSERR_ACCES &&
		    cr->cr_uid == 0 && cr->cr_ruid != 0) {
#ifdef DEBUG
			rfs2call_hits++;
#endif
			cr = crdup(cr);
			cr->cr_uid = cr->cr_ruid;
			rpcerror = rfscall(mi, which, xdrargs, argsp,
					    xdrres, resp, cr, douprintf, NULL);
			crfree(cr);
#ifdef DEBUG
			if (*statusp == NFSERR_ACCES)
				rfs2call_misses++;
#endif
		}
	} else if (rpc_status == RPC_PROCUNAVAIL) {
		*statusp = NFSERR_OPNOTSUPP;
		rpcerror = 0;
	}

	return (rpcerror);
}

#define	NFS3_JUKEBOX_DELAY	10L * HZ

static long nfs3_jukebox_delay = 0;

#ifdef DEBUG
static int rfs3call_hits = 0;
static int rfs3call_misses = 0;
#endif

int
rfs3call(mntinfo_t *mi, int which, xdrproc_t xdrargs, caddr_t argsp,
	xdrproc_t xdrres, caddr_t resp, cred_t *cr, int *douprintf,
	nfsstat3 *statusp)
{
	int rpcerror;
	int user_informed;

	user_informed = 0;
	do {
		rpcerror = rfscall(mi, which, xdrargs, argsp, xdrres, resp,
				    cr, douprintf, NULL);
		if (!rpcerror) {
			if (*statusp == NFS3ERR_JUKEBOX) {
				if (!user_informed) {
					user_informed = 1;
					uprintf(
		"file temporarily unavailable on the server, retrying...\n");
				}
				delay(nfs3_jukebox_delay);
			}
			/*
			 * Boy is this a kludge!  If the reply status is
			 * NFS3ERR_EACCES, it may be because we are root
			 * (no root net access).  Check the real uid, if
			 * it isn't root make that the uid instead and
			 * retry the call.
			 */
			else if (*statusp == NFS3ERR_ACCES &&
			    cr->cr_uid == 0 && cr->cr_ruid != 0) {
#ifdef DEBUG
				rfs3call_hits++;
#endif
				cr = crdup(cr);
				cr->cr_uid = cr->cr_ruid;
				rpcerror = rfscall(mi, which, xdrargs, argsp,
						xdrres, resp, cr, douprintf,
						NULL);
				crfree(cr);
#ifdef DEBUG
				if (*statusp == NFS3ERR_ACCES)
					rfs3call_misses++;
#endif
			}
		}
	} while (!rpcerror && *statusp == NFS3ERR_JUKEBOX);

	return (rpcerror);
}

static int
rfscall(mntinfo_t *mi, int which, xdrproc_t xdrargs, caddr_t argsp,
	xdrproc_t xdrres, caddr_t resp, cred_t *cr, int *douprintf,
	enum clnt_stat *rpc_status)
{
	CLIENT *client;
	struct chtab *ch;
	register enum clnt_stat status;
	struct rpc_err rpcerr;
	struct timeval wait;
	int timeo;		/* in units of HZ */
	int my_rsize, my_wsize;
	bool_t tryagain;
	k_sigset_t smask;
	void sigintr();
	void sigunintr();

	TRACE_2(TR_FAC_NFS, TR_RFSCALL_START,
		"rfscall_start:which %d server %x",
		which, &mi->mi_addr);

	clstat.calls.value.ul++;
	mi->mi_reqs[which].value.ul++;

	rpcerr.re_status = RPC_SUCCESS;

	/*
	 * Remember the transfer sizes in case
	 * nfs_feedback changes them underneath us.
	 */
	my_rsize = mi->mi_curread;
	my_wsize = mi->mi_curwrite;

	/*
	 * clget() calls clnt_tli_kinit() which clears the xid, so we
	 * are guaranteed to reprocess the retry as a new request.
	 */
	rpcerr.re_errno = clget(mi, cr, &client, &ch);
	if (rpcerr.re_errno != 0)
		return (rpcerr.re_errno);

	if (mi->mi_knetconfig->knc_semantics == NC_TPI_COTS_ORD ||
	    mi->mi_knetconfig->knc_semantics == NC_TPI_COTS) {
		timeo = (mi->mi_timeo * HZ) / 10;
	} else {
		mutex_enter(&mi->mi_lock);
		timeo = CLNT_SETTIMERS(client,
			&(mi->mi_timers[mi->mi_timer_type[which]]),
			&(mi->mi_timers[NFS_CALLTYPES]),
			(minimum_timeo[mi->mi_call_type[which]]*HZ)>>3,
			(void (*)()) 0, (caddr_t)mi, 0);
		mutex_exit(&mi->mi_lock);
	}

	/*
	 * If hard mounted fs, retry call forever unless hard error occurs.
	 */

	do {
		tryagain = FALSE;
		wait.tv_sec = timeo / HZ;
		wait.tv_usec = 1000000/HZ * (timeo % HZ);

		/*
		 * Mask out all signals except SIGHUP, SIGINT, SIGQUIT
		 * and SIGTERM. (Preserving the existing masks).
		 * Mask out SIGINT if mount option nointr is specified.
		 */
		sigintr(&smask, mi->mi_flags & MI_INT);

		status = CLNT_CALL(client, which, xdrargs, argsp,
				    xdrres, resp, wait);

		/*
		 * restore original signal mask
		 */
		sigunintr(&smask);

		switch (status) {
		case RPC_SUCCESS:
			(void) nfs_feedback(FEEDBACK_OK, which, mi);
			break;

		/*
		 * Unrecoverable errors: give up immediately
		 */
		case RPC_AUTHERROR:
		case RPC_CANTENCODEARGS:
		case RPC_CANTDECODERES:
		case RPC_VERSMISMATCH:
		case RPC_PROCUNAVAIL:
		case RPC_PROGUNAVAIL:
		case RPC_PROGVERSMISMATCH:
		case RPC_CANTDECODEARGS:
			break;

		case RPC_INTR:
			/*
			 * There is no way to recover from this error,
			 * even if mount option nointr is specified.
			 * SIGKILL, for example, cannot be blocked.
			 */
			rpcerr.re_status = RPC_INTR;
			rpcerr.re_errno = EINTR;
			break;

		default:		/* probably RPC_TIMEDOUT */
			if (!(mi->mi_flags & MI_HARD))
				break;

			tryagain = TRUE;
			timeo = backoff(timeo);
			mutex_enter(&mi->mi_lock);
			if (!(mi->mi_flags & MI_PRINTED)) {
				mi->mi_flags |= MI_PRINTED;
				mutex_exit(&mi->mi_lock);
#ifdef DEBUG
				printf(
			    "NFS%ld server %s not responding still trying\n",
					mi->mi_vers, mi->mi_hostname);
#else
				printf(
			    "NFS server %s not responding still trying\n",
					mi->mi_hostname);
#endif
			} else
				mutex_exit(&mi->mi_lock);
			if (*douprintf && curproc->p_sessp->s_vp != NULL) {
				*douprintf = 0;
#ifdef DEBUG
				uprintf(
			    "NFS%ld server %s not responding still trying\n",
					mi->mi_vers, mi->mi_hostname);
#else
				uprintf(
			    "NFS server %s not responding still trying\n",
					mi->mi_hostname);
#endif
			}

			/*
			 * If doing dynamic adjustment of transfer
			 * size and if it's a read or write call
			 * and if the transfer size changed while
			 * retransmitting or if the feedback routine
			 * changed the transfer size,
			 * then exit rfscall so that the transfer
			 * size can be adjusted at the vnops level.
			 */
			if ((mi->mi_flags & MI_DYNAMIC) &&
			    mi->mi_timer_type[which] != 0 &&
			    (mi->mi_curread != my_rsize ||
			    mi->mi_curwrite != my_wsize ||
			    nfs_feedback(FEEDBACK_REXMIT1, which, mi))) {
				/*
				 * On read or write calls, return
				 * back to the vnode ops level if
				 * the transfer size changed.
				 */
				clfree(client, ch);
				return (ENFS_TRYAGAIN);
			}
		}
	} while (tryagain);

	if (status != RPC_SUCCESS) {
		clstat.badcalls.value.ul++;
		if (status != RPC_INTR) {
			mutex_enter(&mi->mi_lock);
			mi->mi_flags |= MI_DOWN;
			mutex_exit(&mi->mi_lock);
			CLNT_GETERR(client, &rpcerr);
#ifdef DEBUG
			printf(
			"NFS%ld %s failed for server %s: error %d (%s)\n",
				mi->mi_vers, mi->mi_rfsnames[which],
				mi->mi_hostname, status, clnt_sperrno(status));
			if (curproc->p_sessp->s_vp) {
				uprintf(
			"NFS%ld %s failed for server %s: error %d (%s)\n",
					mi->mi_vers, mi->mi_rfsnames[which],
					mi->mi_hostname, status,
					clnt_sperrno(status));
			}
#else
			printf("NFS %s failed for server %s: error %d (%s)\n",
				mi->mi_rfsnames[which], mi->mi_hostname,
				status, clnt_sperrno(status));
			if (curproc->p_sessp->s_vp) {
				uprintf(
				"NFS %s failed for server %s: error %d (%s)\n",
					mi->mi_rfsnames[which],
					mi->mi_hostname, status,
					clnt_sperrno(status));
			}
#endif
		}
	} else {
		/*
		 * Test the value of mi_down and mi_printed without
		 * holding the mi_lock mutex.  If they are both zero,
		 * then it is okay to skip the down and printed
		 * processing.  This saves on a mutex_enter and
		 * mutex_exit pair for a normal, successful RPC.
		 * This was just complete overhead.
		 */
		if (mi->mi_flags & (MI_DOWN | MI_PRINTED)) {
			mutex_enter(&mi->mi_lock);
			mi->mi_flags &= ~MI_DOWN;
			if (mi->mi_flags & MI_PRINTED) {
				mi->mi_flags &= ~MI_PRINTED;
				mutex_exit(&mi->mi_lock);
#ifdef DEBUG
				printf("NFS%ld server %s ok\n", mi->mi_vers,
					mi->mi_hostname);
#else
				printf("NFS server %s ok\n", mi->mi_hostname);
#endif
			} else
				mutex_exit(&mi->mi_lock);
		}

		if (*douprintf == 0) {
#ifdef DEBUG
			uprintf("NFS%ld server %s ok\n", mi->mi_vers,
				mi->mi_hostname);
#else
			uprintf("NFS server %s ok\n", mi->mi_hostname);
#endif
			*douprintf = 1;
		}
	}

	clfree(client, ch);

	ASSERT(rpcerr.re_status == RPC_SUCCESS || rpcerr.re_errno != 0);

	if (rpc_status != NULL)
		*rpc_status = rpcerr.re_status;

	TRACE_1(TR_FAC_NFS, TR_RFSCALL_END, "rfscall_end:errno %d",
		rpcerr.re_errno);

	return (rpcerr.re_errno);
}

#ifdef DEBUG
static int acl2call_hits = 0;
static int acl2call_misses = 0;
#endif

int
acl2call(mntinfo_t *mi, int which, xdrproc_t xdrargs, caddr_t argsp,
	xdrproc_t xdrres, caddr_t resp, cred_t *cr, int *douprintf,
	enum nfsstat *statusp)
{
	int rpcerror;

	rpcerror = aclcall(mi, which, xdrargs, argsp, xdrres, resp,
			    cr, douprintf);
	if (!rpcerror) {
		/*
		 * Boy is this a kludge!  If the reply status is
		 * NFSERR_EACCES, it may be because we are root
		 * (no root net access).  Check the real uid, if
		 * it isn't root make that the uid instead and
		 * retry the call.
		 */
		if (*statusp == NFSERR_ACCES &&
		    cr->cr_uid == 0 && cr->cr_ruid != 0) {
#ifdef DEBUG
			acl2call_hits++;
#endif
			cr = crdup(cr);
			cr->cr_uid = cr->cr_ruid;
			rpcerror = aclcall(mi, which, xdrargs, argsp,
					    xdrres, resp, cr, douprintf);
			crfree(cr);
#ifdef DEBUG
			if (*statusp == NFSERR_ACCES)
				acl2call_misses++;
#endif
		}
	}

	return (rpcerror);
}

#ifdef DEBUG
static int acl3call_hits = 0;
static int acl3call_misses = 0;
#endif

int
acl3call(mntinfo_t *mi, int which, xdrproc_t xdrargs, caddr_t argsp,
	xdrproc_t xdrres, caddr_t resp, cred_t *cr, int *douprintf,
	nfsstat3 *statusp)
{
	int rpcerror;
	int user_informed;

	user_informed = 0;
	do {
		rpcerror = aclcall(mi, which, xdrargs, argsp, xdrres, resp,
				    cr, douprintf);
		if (!rpcerror) {
			if (*statusp == NFS3ERR_JUKEBOX) {
				if (!user_informed) {
					user_informed = 1;
					uprintf(
		"file temporarily unavailable on the server, retrying...\n");
				}
				delay(nfs3_jukebox_delay);
			}
			/*
			 * Boy is this a kludge!  If the reply status is
			 * NFS3ERR_EACCES, it may be because we are root
			 * (no root net access).  Check the real uid, if
			 * it isn't root make that the uid instead and
			 * retry the call.
			 */
			else if (*statusp == NFS3ERR_ACCES &&
			    cr->cr_uid == 0 && cr->cr_ruid != 0) {
#ifdef DEBUG
				acl3call_hits++;
#endif
				cr = crdup(cr);
				cr->cr_uid = cr->cr_ruid;
				rpcerror = aclcall(mi, which, xdrargs, argsp,
						xdrres, resp, cr, douprintf);
				crfree(cr);
#ifdef DEBUG
				if (*statusp == NFS3ERR_ACCES)
					acl3call_misses++;
#endif
			}
		}
	} while (!rpcerror && *statusp == NFS3ERR_JUKEBOX);

	return (rpcerror);
}

static int
aclcall(mntinfo_t *mi, int which, xdrproc_t xdrargs, caddr_t argsp,
	xdrproc_t xdrres, caddr_t resp, cred_t *cr, int *douprintf)
{
	CLIENT *client;
	struct chtab *ch;
	register enum clnt_stat status;
	struct rpc_err rpcerr;
	struct timeval wait;
	int timeo;		/* in units of HZ */
#ifdef notyet
	int my_rsize, my_wsize;
#endif
	bool_t tryagain;
	k_sigset_t smask;
	void sigintr();
	void sigunintr();

#ifdef notyet
	TRACE_2(TR_FAC_NFS, TR_RFSCALL_START,
		"rfscall_start:which %d server %x",
		which, &mi->mi_addr);
#endif

	clstat.calls.value.ul++;
	mi->mi_aclreqs[which].value.ul++;

	rpcerr.re_status = RPC_SUCCESS;

#ifdef notyet
	/*
	 * Remember the transfer sizes in case
	 * nfs_feedback changes them underneath us.
	 */
	my_rsize = mi->mi_curread;
	my_wsize = mi->mi_curwrite;
#endif

	/*
	 * acl_clget() calls clnt_tli_kinit() which clears the xid, so we
	 * are guaranteed to reprocess the retry as a new request.
	 */
	rpcerr.re_errno = acl_clget(mi, cr, &client, &ch);
	if (rpcerr.re_errno != 0)
		return (rpcerr.re_errno);

	if (mi->mi_knetconfig->knc_semantics == NC_TPI_COTS_ORD ||
	    mi->mi_knetconfig->knc_semantics == NC_TPI_COTS) {
		timeo = (mi->mi_timeo * HZ) / 10;
	} else {
		mutex_enter(&mi->mi_lock);
		timeo = CLNT_SETTIMERS(client,
			&(mi->mi_timers[mi->mi_acl_timer_type[which]]),
			&(mi->mi_timers[NFS_CALLTYPES]),
			(minimum_timeo[mi->mi_acl_call_type[which]]*HZ)>>3,
			(void (*)()) 0, (caddr_t)mi, 0);
		mutex_exit(&mi->mi_lock);
	}

	/*
	 * If hard mounted fs, retry call forever unless hard error occurs.
	 */

	do {
		tryagain = FALSE;
		wait.tv_sec = timeo / HZ;
		wait.tv_usec = 1000000/HZ * (timeo % HZ);

		/*
		 * Mask out all signals except SIGHUP, SIGINT, SIGQUIT
		 * and SIGTERM. (Preserving the existing masks).
		 * Mask out SIGINT if mount option nointr is specified.
		 */
		sigintr(&smask, mi->mi_flags & MI_INT);

		status = CLNT_CALL(client, which, xdrargs, argsp,
				    xdrres, resp, wait);

		/*
		 * restore original signal mask
		 */
		sigunintr(&smask);

		switch (status) {
		case RPC_SUCCESS:
#ifdef notyet
			(void) nfs_feedback(FEEDBACK_OK, which, mi);
#endif
			break;

		/*
		 * Unrecoverable errors: give up immediately
		 */
		case RPC_AUTHERROR:
		case RPC_CANTENCODEARGS:
		case RPC_VERSMISMATCH:
		case RPC_PROCUNAVAIL:
		case RPC_PROGVERSMISMATCH:
			break;

		/*
		 * Unfortunately, there are servers in the world which
		 * are not coded correctly.  They are not prepared to
		 * handle RPC requests to the NFS port which are not
		 * NFS requests.  Thus, they may try to process the
		 * NFS_ACL request as if it were an NFS request.  This
		 * does not work.  Generally, an error will be generated
		 * on the client because it will not be able to decode
		 * the response from the server.  However, it seems
		 * possible that the server may not be able to decode
		 * the arguments.  Thus, the criteria for deciding
		 * whether the server supports NFS_ACL or not is whether
		 * the following RPC errors are returned from CLNT_CALL.
		 */
		case RPC_CANTDECODERES:
		case RPC_PROGUNAVAIL:
		case RPC_CANTDECODEARGS:
			mutex_enter(&mi->mi_lock);
			mi->mi_flags &= ~MI_ACL;
			mutex_exit(&mi->mi_lock);
			break;

		case RPC_INTR:
			/*
			 * There is no way to recover from this error,
			 * even if mount option nointr is specified.
			 * SIGKILL, for example, cannot be blocked.
			 */
			rpcerr.re_status = RPC_INTR;
			rpcerr.re_errno = EINTR;
			break;

		default:		/* probably RPC_TIMEDOUT */
			if (!(mi->mi_flags & MI_HARD))
				break;

			tryagain = TRUE;
			timeo = backoff(timeo);
			mutex_enter(&mi->mi_lock);
			if (!(mi->mi_flags & MI_PRINTED)) {
				mi->mi_flags |= MI_PRINTED;
				mutex_exit(&mi->mi_lock);
#ifdef DEBUG
				printf(
			"NFS_ACL%ld server %s not responding still trying\n",
					mi->mi_vers, mi->mi_hostname);
#else
				printf(
			    "NFS server %s not responding still trying\n",
					mi->mi_hostname);
#endif
			} else
				mutex_exit(&mi->mi_lock);
			if (*douprintf && curproc->p_sessp->s_vp != NULL) {
				*douprintf = 0;
#ifdef DEBUG
				uprintf(
			"NFS_ACL%ld server %s not responding still trying\n",
					mi->mi_vers, mi->mi_hostname);
#else
				uprintf(
			    "NFS server %s not responding still trying\n",
					mi->mi_hostname);
#endif
			}

#ifdef notyet
			/*
			 * If doing dynamic adjustment of transfer
			 * size and if it's a read or write call
			 * and if the transfer size changed while
			 * retransmitting or if the feedback routine
			 * changed the transfer size,
			 * then exit rfscall so that the transfer
			 * size can be adjusted at the vnops level.
			 */
			if ((mi->mi_flags & MI_DYNAMIC) &&
			    mi->mi_acl_timer_type[which] != 0 &&
			    (mi->mi_curread != my_rsize ||
			    mi->mi_curwrite != my_wsize ||
			    nfs_feedback(FEEDBACK_REXMIT1, which, mi))) {
				/*
				 * On read or write calls, return
				 * back to the vnode ops level if
				 * the transfer size changed.
				 */
				clfree(client, ch);
				return (ENFS_TRYAGAIN);
			}
#endif
		}
	} while (tryagain);

	if (status != RPC_SUCCESS) {
		clstat.badcalls.value.ul++;
		if (status == RPC_CANTDECODERES ||
		    status == RPC_PROGUNAVAIL ||
		    status == RPC_CANTDECODEARGS)
			CLNT_GETERR(client, &rpcerr);
		else if (status != RPC_INTR) {
			mutex_enter(&mi->mi_lock);
			mi->mi_flags |= MI_DOWN;
			mutex_exit(&mi->mi_lock);
			CLNT_GETERR(client, &rpcerr);
#ifdef DEBUG
			printf(
			"NFS_ACL%ld %s failed for server %s: error %d (%s)\n",
				mi->mi_vers, mi->mi_aclnames[which],
				mi->mi_hostname, status, clnt_sperrno(status));
			if (curproc->p_sessp->s_vp) {
				uprintf(
			"NFS_ACL%ld %s failed for server %s: error %d (%s)\n",
					mi->mi_vers, mi->mi_aclnames[which],
					mi->mi_hostname, status,
					clnt_sperrno(status));
			}
#else
			printf("NFS %s failed for server %s: error %d (%s)\n",
				mi->mi_aclnames[which], mi->mi_hostname,
				status, clnt_sperrno(status));
			if (curproc->p_sessp->s_vp) {
				uprintf(
				"NFS %s failed for server %s: error %d (%s)\n",
					mi->mi_aclnames[which],
					mi->mi_hostname, status,
					clnt_sperrno(status));
			}
#endif
		}
	} else {
		/*
		 * Test the value of mi_down and mi_printed without
		 * holding the mi_lock mutex.  If they are both zero,
		 * then it is okay to skip the down and printed
		 * processing.  This saves on a mutex_enter and
		 * mutex_exit pair for a normal, successful RPC.
		 * This was just complete overhead.
		 */
		if (mi->mi_flags & (MI_DOWN | MI_PRINTED)) {
			mutex_enter(&mi->mi_lock);
			mi->mi_flags &= ~MI_DOWN;
			if (mi->mi_flags & MI_PRINTED) {
				mi->mi_flags &= ~MI_PRINTED;
				mutex_exit(&mi->mi_lock);
#ifdef DEBUG
				printf("NFS_ACL%ld server %s ok\n", mi->mi_vers,
					mi->mi_hostname);
#else
				printf("NFS server %s ok\n", mi->mi_hostname);
#endif
			} else
				mutex_exit(&mi->mi_lock);
		}

		if (*douprintf == 0) {
#ifdef DEBUG
			uprintf("NFS_ACL%ld server %s ok\n", mi->mi_vers,
				mi->mi_hostname);
#else
			uprintf("NFS server %s ok\n", mi->mi_hostname);
#endif
			*douprintf = 1;
		}
	}

	clfree(client, ch);

	ASSERT(rpcerr.re_status == RPC_SUCCESS || rpcerr.re_errno != 0);

#ifdef notyet
	TRACE_1(TR_FAC_NFS, TR_RFSCALL_END, "rfscall_end:errno %d",
		rpcerr.re_errno);
#endif

	return (rpcerr.re_errno);
}

void
vattr_to_sattr(struct vattr *vap, struct nfssattr *sa)
{
	long mask = vap->va_mask;

	if (!(mask & AT_MODE))
		sa->sa_mode = (u_long)-1;
	else
		sa->sa_mode = vap->va_mode;
	if (!(mask & AT_UID))
		sa->sa_uid = (u_long)-1;
	else
		sa->sa_uid = (u_long)vap->va_uid;
	if (!(mask & AT_GID))
		sa->sa_gid = (u_long)-1;
	else
		sa->sa_gid = (u_long)vap->va_gid;
	if (!(mask & AT_SIZE))
		sa->sa_size = (u_long)-1;
	else
		sa->sa_size = vap->va_size;
	if (!(mask & AT_ATIME))
		sa->sa_atime.tv_sec = sa->sa_atime.tv_usec = (long)-1;
	else {
		sa->sa_atime.tv_sec = vap->va_atime.tv_sec;
		sa->sa_atime.tv_usec = vap->va_atime.tv_nsec / 1000;
	}
	if (!(mask & AT_MTIME))
		sa->sa_mtime.tv_sec = sa->sa_mtime.tv_usec = (long)-1;
	else {
		sa->sa_mtime.tv_sec = vap->va_mtime.tv_sec;
		sa->sa_mtime.tv_usec = vap->va_mtime.tv_nsec / 1000;
	}
}

void
vattr_to_sattr3(struct vattr *vap, sattr3 *sa)
{
	long mask = vap->va_mask;

	if (!(mask & AT_MODE))
		sa->mode.set_it = FALSE;
	else {
		sa->mode.set_it = TRUE;
		sa->mode.mode = (mode3)vap->va_mode;
	}
	if (!(mask & AT_UID))
		sa->uid.set_it = FALSE;
	else {
		sa->uid.set_it = TRUE;
		sa->uid.uid = (uid3)vap->va_uid;
	}
	if (!(mask & AT_GID))
		sa->gid.set_it = FALSE;
	else {
		sa->gid.set_it = TRUE;
		sa->gid.gid = (gid3)vap->va_gid;
	}
	if (!(mask & AT_SIZE))
		sa->size.set_it = FALSE;
	else {
		sa->size.set_it = TRUE;
		sa->size.size = (size3)vap->va_size;
	}
	if (!(mask & AT_ATIME))
		sa->atime.set_it = DONT_CHANGE;
	else {
		sa->atime.set_it = SET_TO_CLIENT_TIME;
		sa->atime.atime.seconds = (uint32)vap->va_atime.tv_sec;
		sa->atime.atime.nseconds = (uint32)vap->va_atime.tv_nsec;
	}
	if (!(mask & AT_MTIME))
		sa->mtime.set_it = DONT_CHANGE;
	else {
		sa->mtime.set_it = SET_TO_CLIENT_TIME;
		sa->mtime.mtime.seconds = (uint32)vap->va_mtime.tv_sec;
		sa->mtime.mtime.nseconds = (uint32)vap->va_mtime.tv_nsec;
	}
}

void
setdiropargs(struct nfsdiropargs *da, char *nm, vnode_t *dvp)
{

	da->da_fhandle = VTOFH(dvp);
	da->da_name = nm;
	da->da_flags = 0;
}

void
setdiropargs3(diropargs3 *da, char *nm, vnode_t *dvp)
{

	da->dir = *VTOFH3(dvp);
	da->name = nm;
}

gid_t
setdirgid(vnode_t *dvp, cred_t *cr)
{
	rnode_t *rp;
	gid_t gid;

	/*
	 * To determine the expected group-id of the created file:
	 *  1)	If the filesystem was not mounted with the Old-BSD-compatible
	 *	GRPID option, and the directory's set-gid bit is clear,
	 *	then use the process's gid.
	 *  2)	Otherwise, set the group-id to the gid of the parent directory.
	 */
	rp = VTOR(dvp);
	mutex_enter(&rp->r_statelock);
	if (!(VTOMI(dvp)->mi_flags & MI_GRPID) &&
	    !(rp->r_attr.va_mode & VSGID))
		gid = cr->cr_gid;
	else
		gid = rp->r_attr.va_gid;
	mutex_exit(&rp->r_statelock);
	return (gid);
}

mode_t
setdirmode(vnode_t *dvp, mode_t om)
{

	/*
	 * Modify the expected mode (om) so that the set-gid bit matches
	 * that of the parent directory (dvp).
	 */
	if (VTOR(dvp)->r_attr.va_mode & VSGID)
		om |= VSGID;
	else
		om &= ~VSGID;
	return (om);
}

/*
 * Free the resources associated with an rnode.
 *
 * There are no special mutex requirements for this routine.  The
 * nfs_rtable_lock can be held, but is not required.  The routine
 * does make the asumption that r_statelock in the rnode is not
 * held on entry to this routine.
 */
static void
rinactive(rnode_t *rp, cred_t *cr)
{
	vnode_t *vp;
	cred_t *cred;
	access_cache *acp, *nacp;
	rddir_cache *rdc, *nrdc;
	char *contents;
	int size;
	vsecattr_t *vsp;
	int error;

	/*
	 * Before freeing anything, wait until all asynchronous
	 * activity is done on this rnode.  This will allow all
	 * asynchronous read ahead and write behind i/o's to
	 * finish.
	 */
	mutex_enter(&rp->r_statelock);
	while (rp->r_count > 0)
		cv_wait(&rp->r_cv, &rp->r_statelock);
	mutex_exit(&rp->r_statelock);

	/*
	 * Flush and invalidate all pages associated with the vnode.
	 */
	vp = RTOV(rp);
	if (vp->v_pages != NULL) {
		ASSERT(vp->v_type != VCHR);
		if ((rp->r_flags & RDIRTY) && !rp->r_error) {
			error = VOP_PUTPAGE(vp, (offset_t)0, 0, 0, cr);
			if (error && (error == ENOSPC || error == EDQUOT)) {
				mutex_enter(&rp->r_statelock);
				if (!rp->r_error)
					rp->r_error = error;
				mutex_exit(&rp->r_statelock);
			}
		}
		nfs_invalidate_pages(vp, 0, cr);
	}

	/*
	 * Free any held credentials and caches which may be associated
	 * with this rnode.
	 */
	mutex_enter(&rp->r_statelock);
	cred = rp->r_cred;
	rp->r_cred = NULL;
	acp = rp->r_acc;
	rp->r_acc = NULL;
	rdc = rp->r_dir;
	rp->r_dir = NULL;
	rp->r_direof = NULL;
	contents = rp->r_symlink.contents;
	size = rp->r_symlink.size;
	rp->r_symlink.contents = NULL;
	vsp = rp->r_secattr;
	rp->r_secattr = NULL;
	mutex_exit(&rp->r_statelock);

	/*
	 * Free the held credential.
	 */
	if (cred != NULL)
		crfree(cred);

	/*
	 * Free the access cache entries.
	 */
	while (acp != NULL) {
		crfree(acp->cred);
		nacp = acp->next;
#ifdef DEBUG
		access_cache_free((void *)acp, sizeof (*acp));
#else
		kmem_free((caddr_t)acp, sizeof (*acp));
#endif
		acp = nacp;
	}

	/*
	 * Free the readdir cache entries.
	 */
	while (rdc != NULL) {
		nrdc = rdc->next;
		mutex_enter(&rp->r_statelock);
		while (rdc->flags & RDDIR) {
			rdc->flags |= RDDIRWAIT;
			cv_wait(&rdc->cv, &rp->r_statelock);
		}
		mutex_exit(&rp->r_statelock);
		if (rdc->entries != NULL)
			kmem_free(rdc->entries, rdc->buflen);
		cv_destroy(&rdc->cv);
#ifdef DEBUG
		rddir_cache_free((void *)rdc, sizeof (*rdc));
#else
		kmem_free((caddr_t)rdc, sizeof (*rdc));
#endif
		rdc = nrdc;
	}

	/*
	 * Free the symbolic link cache.
	 */
	if (contents != NULL) {
#ifdef DEBUG
		symlink_cache_free((void *)contents, size);
#else
		kmem_free(contents, size);
#endif
	}

	/*
	 * Free any cached ACL.
	 */
	if (vsp != NULL)
		nfs_acl_free(vsp);
}

/*
 * Return a vnode for the given NFS Version 2 file handle.
 * If no rnode exists for this fhandle, create one and put it
 * into the hash queues.  If the rnode for this fhandle
 * already exists, return it.
 */
vnode_t *
makenfsnode(fhandle_t *fh, struct nfsfattr *attr, struct vfs *vfsp, cred_t *cr)
{
	int newnode;
	vnode_t *vp;
	nfs_fhandle nfh;
	long seq;

	nfh.fh_len = NFS_FHSIZE;
	bcopy((caddr_t)fh, nfh.fh_buf, NFS_FHSIZE);
	mutex_enter(&nfs_rtable_lock);
	vp = make_rnode(&nfh, vfsp, &nfs_vnodeops, nfs_putapage, &newnode, cr);
	if (attr != NULL) {
		if (!newnode) {
			timestruc_t ctime;
			timestruc_t mtime;

			ctime.tv_sec = attr->na_ctime.tv_sec;
			ctime.tv_nsec = attr->na_ctime.tv_usec*1000;
			mtime.tv_sec = attr->na_mtime.tv_sec;
			mtime.tv_nsec = attr->na_mtime.tv_usec*1000;
			mutex_exit(&nfs_rtable_lock);
			nfs_cache_check(vp, ctime, mtime, attr->na_size,
					&seq, cr);
			nfs_attrcache(vp, attr, seq);
		} else {
			vp->v_type = n2v_type(attr);
			/*
			 * A translation here seems to be necessary
			 * because this function can be called
			 * with `attr' that has come from the wire,
			 * and been operated on by vattr_to_nattr().
			 * See nfsrootvp()->VOP_GETTATTR()->nfsgetattr()
			 * ->nfs_getattr_otw()->rfscall()->vattr_to_nattr()
			 * ->makenfsnode().
			 */
			if ((attr->na_rdev & 0xffff0000) == 0)
				vp->v_rdev = nfsv2_expdev(attr->na_rdev);
			else
				vp->v_rdev = n2v_rdev(attr);
			ASSERT(VTOR(vp)->r_seq == 0);
			nfs_attrcache(vp, attr, 0);
			mutex_exit(&nfs_rtable_lock);
		}
	} else {
		if (newnode)
			PURGE_ATTRCACHE(vp);
		mutex_exit(&nfs_rtable_lock);
	}

	return (vp);
}

/*
 * Return a vnode for the given NFS Version 3 file handle.
 * If no rnode exists for this fhandle, create one and put it
 * into the hash queues.  If the rnode for this fhandle
 * already exists, return it.
 */
vnode_t *
makenfs3node(nfs_fh3 *fh, fattr3 *attr, struct vfs *vfsp, cred_t *cr)
{
	int newnode;
	vnode_t *vp;
	long seq;

	mutex_enter(&nfs_rtable_lock);
	vp = make_rnode((nfs_fhandle *)fh, vfsp, &nfs3_vnodeops, nfs3_putapage,
			&newnode, cr);
	if (attr != NULL) {
		if (!newnode) {
			mutex_exit(&nfs_rtable_lock);
			nfs3_cache_check_fattr3(vp, attr, &seq, cr);
			nfs3_attrcache(vp, attr, seq);
		} else {
			vp->v_type = nf3_to_vt[attr->type];
			vp->v_rdev = makedevice(attr->rdev.specdata1,
						attr->rdev.specdata2);
			ASSERT(VTOR(vp)->r_seq == 0);
			nfs3_attrcache(vp, attr, 0);
			mutex_exit(&nfs_rtable_lock);
		}
	} else {
		if (newnode)
			PURGE_ATTRCACHE(vp);
		mutex_exit(&nfs_rtable_lock);
	}

	return (vp);
}

static int
rtablehash(nfs_fhandle *fh)
{
	int sum;
	char *cp, *ecp;

	cp = fh->fh_buf;
	ecp = &fh->fh_buf[fh->fh_len];
	sum = 0;
	while (cp < ecp)
		sum += *cp++;
	return (sum & rtablemask);
}

static vnode_t *
make_rnode(nfs_fhandle *fh, struct vfs *vfsp, struct vnodeops *vops,
	int (*putapage)(vnode_t *, page_t *, u_int *, u_int *, int, cred_t *),
	int *newnode, cred_t *cr)
{
	rnode_t *rp;
	rnode_t *trp;
	vnode_t *vp;

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));

start:
	if ((rp = rfind(fh, vfsp)) != NULL) {
		*newnode = 0;
		return (RTOV(rp));
	}
	if (rpfreelist != NULL && rnew >= nrnode) {
		rp = rpfreelist;
		rp_rmfree(rp);
		if (rp->r_flags & RHASHED)
			rp_rmhash(rp);
		vp = RTOV(rp);
		VN_HOLD(vp);
		mutex_exit(&nfs_rtable_lock);
		rinactive(rp, cr);
		mutex_enter(&nfs_rtable_lock);

		mutex_enter(&vp->v_lock);
		if (vp->v_count > 1) {
			vp->v_count--;
			mutex_exit(&vp->v_lock);
			goto start;
		}
		mutex_exit(&vp->v_lock);

		/*
		 * There is a race condition if someone else
		 * alloc's the rnode while we're asleep, so we
		 * check again and recover if found.
		 */
		if ((trp = rfind(fh, vfsp)) != NULL) {
			*newnode = 0;
			rp_addfree(rp, cr);
			return (RTOV(trp));
		}

		/*
		 * destroy old locks before bzero'ing and
		 * recreating the locks below.
		 */
		rw_destroy(&rp->r_rwlock);
		mutex_destroy(&rp->r_statelock);
		cv_destroy(&rp->r_cv);
		cv_destroy(&rp->r_commit.c_cv);
		mutex_destroy(&vp->v_lock);
		cv_destroy(&vp->v_cv);
	} else {
		rp = (rnode_t *)kmem_cache_alloc(rnode_cache, KM_NOSLEEP);
		if (rp == NULL) {
			mutex_exit(&nfs_rtable_lock);
			rp = (rnode_t *)kmem_cache_alloc(rnode_cache, KM_SLEEP);
			mutex_enter(&nfs_rtable_lock);
			/*
			 * There is a race condition if someone else
			 * alloc's the rnode while we're asleep, so we
			 * check again and recover if found.
			 */
			if ((trp = rfind(fh, vfsp)) != NULL) {
				*newnode = 0;
				kmem_cache_free(rnode_cache, (void *)rp);
				return (RTOV(trp));
			}
		}
		rnew++;
#ifdef DEBUG
		clstat.nrnode.value.ul++;
#endif
		vp = RTOV(rp);
	}
	bzero((caddr_t)rp, sizeof (*rp));
	rw_init(&rp->r_rwlock, "rnode rwlock", RW_DEFAULT, NULL);
	mutex_init(&rp->r_statelock, "rnode state mutex", MUTEX_DEFAULT, NULL);
	cv_init(&rp->r_cv, "rnode cv", CV_DEFAULT, NULL);
	cv_init(&rp->r_commit.c_cv, "rnode c_cv", CV_DEFAULT, NULL);
	rp->r_fh.fh_len = fh->fh_len;
	bcopy(fh->fh_buf, rp->r_fh.fh_buf, fh->fh_len);
	rp->r_putapage = putapage;
	mutex_init(&vp->v_lock, "rnode v_lock", MUTEX_DEFAULT, DEFAULT_WT);
	cv_init(&vp->v_cv, "rnode v_cv", CV_DEFAULT, NULL);
	vp->v_count = 1;
	vp->v_op = vops;
	vp->v_data = (caddr_t)rp;
	vp->v_vfsp = vfsp;
	vp->v_type = VNON;
	rp_addhash(rp);
	*newnode = 1;
	return (vp);
}

/*
 * Put an rnode on the free list.
 *
 * The caller must be holding nfs_rtable_lock.
 *
 * Rnodes which were allocated above and beyond the normal limit
 * are immediately freed.
 */
void
rp_addfree(rnode_t *rp, cred_t *cr)
{
	vnode_t *vp;

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));

	vp = RTOV(rp);
	ASSERT(vp->v_count >= 1);

	/*
	 * If someone else has grabbed a reference to this vnode
	 * or if this rnode is already on the freelist, then just
	 * release our reference to the vnode.  The rnode can be
	 * on the freelist already because it is possible for an
	 * rnode to be free, but have modified pages associated
	 * with it.  Thus, the vnode for the page could be held
	 * and then released causing a call to this routine via
	 * the file system inactive routine.
	 */
	mutex_enter(&vp->v_lock);
	if (vp->v_count > 1 || rp->r_freef != NULL) {
		vp->v_count--;
		mutex_exit(&vp->v_lock);
		return;
	}

	/*
	 * If we have too many rnodes allocated and there are no
	 * references to this rnode, or if the rnode is no longer
	 * accessible by it does not reside in the hash queues,
	 * or if an i/o error occurred while writing to the file,
	 * then just free it instead of putting it on the rnode
	 * freelist.  nfs_rtable_lock should not be held while
	 * freeing the resources associated with this rnode because
	 * some of the freeing operations may take a long time.
	 * Thus, nfs_rtable_lock is dropped while freeing the resources
	 * and then reacquired.
	 */
	if ((rnew > nrnode && rp->r_count == 0) ||
	    !(rp->r_flags & RHASHED) ||
	    rp->r_error) {
		mutex_exit(&vp->v_lock);
		if (rp->r_flags & RHASHED)
			rp_rmhash(rp);
		mutex_exit(&nfs_rtable_lock);
		rinactive(rp, cr);
		mutex_enter(&nfs_rtable_lock);
		/*
		 * Recheck the vnode reference count.  We need to
		 * make sure that another reference has not been
		 * acquired while we were not holding v_lock.  The
		 * rnode is not in the rnode hash queues, so the
		 * only way for a reference to have been acquired
		 * is for a VOP_PUTPAGE because the rnode was marked
		 * with RDIRTY or for a modified page.  This
		 * reference may have been acquired before our call
		 * to rinactive.  The i/o may have been completed,
		 * thus allowing rinactive to complete, but the
		 * reference to the vnode may not have been released
		 * yet.  In any case, the rnode can not be destroyed
		 * until the other references to this vnode have been
		 * released.  The other references will take care of
		 * either destroying the rnode or placing it on the
		 * rnode freelist.  If there are no other references,
		 * then the rnode may be safely destroyed.
		 */
		mutex_enter(&vp->v_lock);
		if (vp->v_count > 1) {
			vp->v_count--;
			mutex_exit(&vp->v_lock);
			return;
		}
		ASSERT(vp->v_count == 1);
		ASSERT(rp->r_count == 0);
		ASSERT(rp->r_lmpl == NULL);
		mutex_exit(&vp->v_lock);
		rnew--;
#ifdef DEBUG
		clstat.nrnode.value.ul--;
#endif
		rw_destroy(&rp->r_rwlock);
		mutex_destroy(&rp->r_statelock);
		cv_destroy(&rp->r_cv);
		cv_destroy(&rp->r_commit.c_cv);
		mutex_destroy(&vp->v_lock);
		cv_destroy(&vp->v_cv);
		kmem_cache_free(rnode_cache, (void *)rp);
		return;
	}

	/*
	 * The vnode is not currently referenced by anyone else,
	 * so release this reference and place the rnode on the
	 * freelist.  If there is no cached data or metadata for
	 * this file, then put the rnode on the front of the
	 * freelist so that it will be reused before other rnodes
	 * which may have cached data or metadata associated with
	 * them.
	 */
	ASSERT(rp->r_lmpl == NULL);
	vp->v_count--;
	mutex_exit(&vp->v_lock);

	if (rpfreelist == NULL) {
		rp->r_freef = rp;
		rp->r_freeb = rp;
		rpfreelist = rp;
	} else {
		rp->r_freef = rpfreelist;
		rp->r_freeb = rpfreelist->r_freeb;
		rpfreelist->r_freeb->r_freef = rp;
		rpfreelist->r_freeb = rp;
		if (vp->v_pages == NULL &&
		    rp->r_acc == NULL &&
		    rp->r_dir == NULL &&
		    rp->r_symlink.contents == NULL &&
		    rp->r_secattr == NULL)
			rpfreelist = rp;
	}
}

/*
 * Remove an rnode from the free list.
 *
 * The caller must be holding the nfs_rtable_lock and the rnode
 * must be on the freelist.
 */
static void
rp_rmfree(rnode_t *rp)
{

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));
	ASSERT(rp->r_freef != NULL && rp->r_freeb != NULL);

	if (rp == rpfreelist) {
		rpfreelist = rp->r_freef;
		if (rp == rpfreelist)
			rpfreelist = NULL;
	}
	rp->r_freeb->r_freef = rp->r_freef;
	rp->r_freef->r_freeb = rp->r_freeb;
	rp->r_freef = rp->r_freeb = NULL;
}

/*
 * Put a rnode in the hash table.
 *
 * The caller must be holding the nfs_rtable_lock.
 */
void
rp_addhash(rnode_t *rp)
{
	int hash;

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));
	ASSERT(!(rp->r_flags & RHASHED));

	hash = rtablehash(&rp->r_fh);
	rp->r_hash = rtable[hash];
	rtable[hash] = rp;
#ifdef DEBUG
	rtablecnt[hash]++;
#endif
	mutex_enter(&rp->r_statelock);
	rp->r_flags |= RHASHED;
	mutex_exit(&rp->r_statelock);
}

/*
 * Remove a rnode from the hash table.
 *
 * The caller must be holding the nfs_rtable_lock.
 */
void
rp_rmhash(rnode_t *rp)
{
	int hash;
	register rnode_t *rt;
	register rnode_t *rtprev = NULL;

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));
	ASSERT(rp->r_flags & RHASHED);

	hash = rtablehash(&rp->r_fh);
	rt = rtable[hash];
	while (rt != NULL) {
		if (rt == rp) {
			if (rtprev == NULL)
				rtable[hash] = rt->r_hash;
			else
				rtprev->r_hash = rt->r_hash;
#ifdef DEBUG
			rtablecnt[hash]--;
#endif
			mutex_enter(&rp->r_statelock);
			rp->r_flags &= ~RHASHED;
			mutex_exit(&rp->r_statelock);
			return;
		}
		rtprev = rt;
		rt = rt->r_hash;
	}
	cmn_err(CE_PANIC, "rp_rmhash: rnode not in hash queue");
}

/*
 * Lookup a rnode by fhandle.
 *
 * The caller must be holding the nfs_rtable_lock.
 */
static rnode_t *
rfind(nfs_fhandle *fh, struct vfs *vfsp)
{
	register rnode_t *rp;
	register vnode_t *vp;

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));

	rp = rtable[rtablehash(fh)];
	while (rp != NULL) {
		vp = RTOV(rp);
		if (vp->v_vfsp == vfsp &&
		    rp->r_fh.fh_len == fh->fh_len &&
		    bcmp(rp->r_fh.fh_buf, fh->fh_buf, fh->fh_len) == 0) {
			VN_HOLD(vp);
			/*
			 * remove rnode from free list, if necessary.
			 */
			if (rp->r_freef != NULL)
				rp_rmfree(rp);
			return (rp);
		}
		rp = rp->r_hash;
	}
	return (NULL);
}

/*
 * Return 1 if there is a active vnode belonging to this vfs in the
 * rtable cache.
 *
 * The caller must be holding the nfs_rtable_lock.
 */
int
check_rtable(struct vfs *vfsp, vnode_t *rootvp)
{
	register rnode_t **rpp, **erpp, *rp;
	register vnode_t *vp;

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));

	erpp = &rtable[rtablesize];
	for (rpp = rtable; rpp < erpp; rpp++) {
		for (rp = *rpp; rp != NULL; rp = rp->r_hash) {
			vp = RTOV(rp);
			if (vp->v_vfsp == vfsp && vp != rootvp) {
				if (rp->r_freef == NULL ||
				    (rp->r_flags & RDIRTY) ||
				    rp->r_count > 0)
					return (1);
			}
		}
	}
	return (0);
}

/*
 * Remove inactive vnodes from the hash queues which belong to this vfs.
 * All of the vnodes should be inactive.
 *
 * The caller must be holding the nfs_rtable_lock.
 */
void
purge_rtable(struct vfs *vfsp, cred_t *cr)
{
	register rnode_t **rpp, **erpp, *rp, *rpprev;

	ASSERT(MUTEX_HELD(&nfs_rtable_lock));

	erpp = &rtable[rtablesize];
	for (rpp = rtable; rpp < erpp; rpp++) {
		rpprev = NULL;
		for (rp = *rpp; rp != NULL; rp = rp->r_hash) {
			if (RTOV(rp)->v_vfsp == vfsp) {
				if (rpprev == NULL)
					*rpp = rp->r_hash;
				else
					rpprev->r_hash = rp->r_hash;
#ifdef DEBUG
				rtablecnt[rpp - rtable]--;
#endif
				mutex_enter(&rp->r_statelock);
				rp->r_flags &= ~RHASHED;
				mutex_exit(&rp->r_statelock);
				rinactive(rp, cr);
			} else
				rpprev = rp;
		}
	}
}

/*
 * Flush all vnodes in this (or every) vfs.
 * Used by nfs_sync and by nfs_unmount.
 */
void
rflush(struct vfs *vfsp, cred_t *cr)
{
	rnode_t **rpp, **erpp, *rp;
	vnode_t *vp, **vplist;
	int num, cnt;

	/*
	 * Check to see whether there is anything to do.
	 */
	num = rnew;
	if (num == 0)
		return;

	/*
	 * Allocate a slot for all currently active rnodes on the
	 * supposition that they all may need flushing.
	 */
	vplist = (vnode_t **)kmem_alloc(num * sizeof (*vplist), KM_SLEEP);
	cnt = 0;

	/*
	 * Walk the hash queues looking for rnodes with page
	 * lists associated with them.  Make a list of these
	 * files.
	 */
	erpp = &rtable[rtablesize];
	mutex_enter(&nfs_rtable_lock);
	for (rpp = rtable; rpp < erpp; rpp++) {
		for (rp = *rpp; rp != NULL; rp = rp->r_hash) {
			vp = RTOV(rp);
			/*
			 * Don't bother sync'ing a vp if it
			 * is part of virtual swap device or
			 * if VFS is read-only
			 */
			if (IS_SWAPVP(vp) ||
			    (vp->v_vfsp->vfs_flag & VFS_RDONLY))
				continue;
			/*
			 * If flushing all mounted file systems or
			 * the vnode belongs to this vfs, has pages
			 * and is marked as either dirty or mmap'd,
			 * hold and add this vnode to the list of
			 * vnodes to flush.
			 */
			if ((vfsp == NULL || vp->v_vfsp == vfsp) &&
			    vp->v_pages != NULL &&
			    ((rp->r_flags & RDIRTY) || rp->r_mapcnt > 0)) {
				VN_HOLD(vp);
				vplist[cnt++] = vp;
				if (cnt == num)
					goto toomany;
			}
		}
	}
toomany:
	mutex_exit(&nfs_rtable_lock);

	/*
	 * Flush and release all of the files on the list.
	 */
	while (cnt-- > 0) {
		vp = vplist[cnt];
		(void) VOP_PUTPAGE(vp, (offset_t)0, 0, B_ASYNC, cr);
		VN_RELE(vp);
	}

	/*
	 * Free the space allocated to hold the list.
	 */
	kmem_free((caddr_t)vplist, num * sizeof (*vplist));
}

static char prefix[] = ".nfs";

static kmutex_t newnum_lock;

int
newnum(void)
{
	static uint newnum = 0;
	register uint id;

	mutex_enter(&newnum_lock);
	if (newnum == 0)
		newnum = hrestime.tv_sec & 0xffff;
	id = newnum++;
	mutex_exit(&newnum_lock);
	return (id);
}

char *
newname(void)
{
	char *news;
	register char *s, *p;
	register uint id;

	id = newnum();
	news = (char *)kmem_alloc((u_int)NFS_MAXNAMLEN, KM_SLEEP);
	s = news;
	p = prefix;
	while (*p != '\0')
		*s++ = *p++;
	while (id != 0) {
		*s++ = "0123456789ABCDEF"[id & 0x0f];
		id >>= 4;
	}
	*s = '\0';
	return (news);
}

int
nfs_atoi(char *cp)
{
	int n;

	n = 0;
	while (*cp != '\0') {
		n = n * 10 + (*cp - '0');
		cp++;
	}

	return (n);
}

int
nfs_subrinit(void)
{
	int i;
	struct unixauthent *ua;
	int num_rnode;
	extern int maxusers;
	extern int max_nprocs;

	num_rnode = (max_nprocs + 16 + maxusers) + 64;

	/*
	 * Allocate and initialize the rnode hash queues
	 */
	if (nrnode == 0)
		nrnode = num_rnode;
	rtablesize = 1 << (highbit(num_rnode / hashlen) - 1);
	rtablemask = rtablesize - 1;
	rtable = (rnode_t **)kmem_zalloc(rtablesize * sizeof (*rtable),
					KM_SLEEP);
#ifdef DEBUG
	rtablecnt = (int *)kmem_zalloc(rtablesize * sizeof (*rtablecnt),
					KM_SLEEP);
#endif
	rnode_cache = kmem_cache_create("rnode_cache", sizeof (rnode_t),
					0, NULL, NULL, nfs_reclaim);

	for (i = 0; i < MAXCLIENTS; i++) {
		ua = (struct unixauthent *) kmem_alloc(sizeof (*ua), KM_SLEEP);
		ua->ua_auth = NULL;
		ua->ua_next = unixauthtab;
		unixauthtab = ua;
	}
	unixauthlist = unixauthtab;

	/*
	 * Initialize the various mutexes
	 */
	mutex_init(&chtable_lock, "chtable_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&unixauthtab_lock, "unixauthtab_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&desauthtab_lock, "desauthtab_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&kerbauthtab_lock, "kerbauthtab_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&nfs_rtable_lock, "nfs_rtable_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&newnum_lock, "newnum_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&nfs_minor_lock, "nfs minor lock", MUTEX_DEFAULT, NULL);
#ifdef DEBUG
	mutex_init(&nfs_accurate_stats, "nfs_accurate_stats", MUTEX_DEFAULT,
		NULL);
#endif

	/*
	 * Assign unique major number for all nfs mounts
	 */
	if ((nfs_major = getudev()) == -1) {
		cmn_err(CE_WARN, "nfs: init: can't get unique device number");
		nfs_major = 0;
	}
	nfs_minor = 0;

	if (nfs3_jukebox_delay == 0L)
		nfs3_jukebox_delay = NFS3_JUKEBOX_DELAY;

	return (0);
}

enum nfsstat
puterrno(int error)
{

	switch (error) {
	case EOPNOTSUPP:
		return (NFSERR_OPNOTSUPP);

	case ENAMETOOLONG:
		return (NFSERR_NAMETOOLONG);

	case ENOTEMPTY:
		return (NFSERR_NOTEMPTY);

	case EDQUOT:
		return (NFSERR_DQUOT);

	case ESTALE:
		return (NFSERR_STALE);

	case EREMOTE:
		return (NFSERR_REMOTE);

	case ENOSYS:
		return (NFSERR_OPNOTSUPP);

	default:
		return ((enum nfsstat) error);
	}
	/* NOTREACHED */
}

int
geterrno(enum nfsstat status)
{

	switch ((int) status) {
	case NFSERR_OPNOTSUPP:
		return (EOPNOTSUPP);

	case NFSERR_NAMETOOLONG:
		return (ENAMETOOLONG);

	case NFSERR_NOTEMPTY:
		return (ENOTEMPTY);

	case NFSERR_DQUOT:
		return (EDQUOT);

	case NFSERR_STALE:
		return (ESTALE);

	case NFSERR_REMOTE:
		return (EREMOTE);

	case NFSERR_WFLUSH:
		return (EIO);

	default:
		return ((int) status);
	}
	/* NOTREACHED */
}

enum nfsstat3
puterrno3(int error)
{

#ifdef DEBUG
	switch (error) {
	case 0:
		return (NFS3_OK);
	case EPERM:
		return (NFS3ERR_PERM);
	case ENOENT:
		return (NFS3ERR_NOENT);
	case EIO:
		return (NFS3ERR_IO);
	case ENXIO:
		return (NFS3ERR_NXIO);
	case EACCES:
		return (NFS3ERR_ACCES);
	case EEXIST:
		return (NFS3ERR_EXIST);
	case EXDEV:
		return (NFS3ERR_XDEV);
	case ENODEV:
		return (NFS3ERR_NODEV);
	case ENOTDIR:
		return (NFS3ERR_NOTDIR);
	case EISDIR:
		return (NFS3ERR_ISDIR);
	case EINVAL:
		return (NFS3ERR_INVAL);
	case EFBIG:
		return (NFS3ERR_FBIG);
	case ENOSPC:
		return (NFS3ERR_NOSPC);
	case EROFS:
		return (NFS3ERR_ROFS);
	case EMLINK:
		return (NFS3ERR_MLINK);
	case ENAMETOOLONG:
		return (NFS3ERR_NAMETOOLONG);
	case ENOTEMPTY:
		return (NFS3ERR_NOTEMPTY);
	case EDQUOT:
		return (NFS3ERR_DQUOT);
	case ESTALE:
		return (NFS3ERR_STALE);
	case EREMOTE:
		return (NFS3ERR_REMOTE);
	case EOPNOTSUPP:
		return (NFS3ERR_NOTSUPP);
	default:
		cmn_err(CE_WARN, "puterrno3: got error %d", error);
		return ((enum nfsstat3) error);
	}
#else
	switch (error) {
	case ENAMETOOLONG:
		return (NFS3ERR_NAMETOOLONG);
	case ENOTEMPTY:
		return (NFS3ERR_NOTEMPTY);
	case EDQUOT:
		return (NFS3ERR_DQUOT);
	case ESTALE:
		return (NFS3ERR_STALE);
	case EOPNOTSUPP:
		return (NFS3ERR_NOTSUPP);
	case EREMOTE:
		return (NFS3ERR_REMOTE);
	default:
		return ((enum nfsstat3) error);
	}
#endif
}

int
geterrno3(enum nfsstat3 status)
{

#ifdef DEBUG
	switch (status) {
	case NFS3_OK:
		return (0);
	case NFS3ERR_PERM:
		return (EPERM);
	case NFS3ERR_NOENT:
		return (ENOENT);
	case NFS3ERR_IO:
		return (EIO);
	case NFS3ERR_NXIO:
		return (ENXIO);
	case NFS3ERR_ACCES:
		return (EACCES);
	case NFS3ERR_EXIST:
		return (EEXIST);
	case NFS3ERR_XDEV:
		return (EXDEV);
	case NFS3ERR_NODEV:
		return (ENODEV);
	case NFS3ERR_NOTDIR:
		return (ENOTDIR);
	case NFS3ERR_ISDIR:
		return (EISDIR);
	case NFS3ERR_INVAL:
		return (EINVAL);
	case NFS3ERR_FBIG:
		return (EFBIG);
	case NFS3ERR_NOSPC:
		return (ENOSPC);
	case NFS3ERR_ROFS:
		return (EROFS);
	case NFS3ERR_MLINK:
		return (EMLINK);
	case NFS3ERR_NAMETOOLONG:
		return (ENAMETOOLONG);
	case NFS3ERR_NOTEMPTY:
		return (ENOTEMPTY);
	case NFS3ERR_DQUOT:
		return (EDQUOT);
	case NFS3ERR_STALE:
		return (ESTALE);
	case NFS3ERR_REMOTE:
		return (EREMOTE);
	case NFS3ERR_BADHANDLE:
		return (ESTALE);
	case NFS3ERR_NOT_SYNC:
		return (EINVAL);
	case NFS3ERR_BAD_COOKIE:
		return (EINVAL);
	case NFS3ERR_NOTSUPP:
		return (EOPNOTSUPP);
	case NFS3ERR_TOOSMALL:
		return (EINVAL);
	case NFS3ERR_SERVERFAULT:
		return (EIO);
	case NFS3ERR_BADTYPE:
		return (EINVAL);
	case NFS3ERR_JUKEBOX:
		return (ENXIO);
	default:
		cmn_err(CE_WARN, "geterrno3: got status %d", status);
		return ((int) status);
	}
#else
	switch (status) {
	case NFS3ERR_NAMETOOLONG:
		return (ENAMETOOLONG);
	case NFS3ERR_NOTEMPTY:
		return (ENOTEMPTY);
	case NFS3ERR_DQUOT:
		return (EDQUOT);
	case NFS3ERR_STALE:
	case NFS3ERR_BADHANDLE:
		return (ESTALE);
	case NFS3ERR_NOTSUPP:
		return (EOPNOTSUPP);
	case NFS3ERR_REMOTE:
		return (EREMOTE);
	case NFS3ERR_NOT_SYNC:
	case NFS3ERR_BAD_COOKIE:
	case NFS3ERR_TOOSMALL:
	case NFS3ERR_BADTYPE:
		return (EINVAL);
	case NFS3ERR_SERVERFAULT:
		return (EIO);
	default:
		return ((int) status);
	}
#endif
}

#ifdef DEBUG
access_cache *
access_cache_alloc(size_t size, int flags)
{
	access_cache *acp;

	acp = (access_cache *) kmem_alloc(size, flags);
	if (acp != NULL) {
		mutex_enter(&nfs_accurate_stats);
		clstat.access.value.ul++;
		mutex_exit(&nfs_accurate_stats);
	}
	return (acp);
}

void
access_cache_free(void *addr, size_t size)
{

	mutex_enter(&nfs_accurate_stats);
	clstat.access.value.ul--;
	mutex_exit(&nfs_accurate_stats);
	kmem_free(addr, size);
}

rddir_cache *
rddir_cache_alloc(size_t size, int flags)
{
	rddir_cache *rc;

	rc = (rddir_cache *) kmem_alloc(size, flags);
	if (rc != NULL) {
		mutex_enter(&nfs_accurate_stats);
		clstat.dirent.value.ul++;
		mutex_exit(&nfs_accurate_stats);
	}
	return (rc);
}

void
rddir_cache_free(void *addr, size_t size)
{

	mutex_enter(&nfs_accurate_stats);
	clstat.dirent.value.ul--;
	mutex_exit(&nfs_accurate_stats);
	kmem_free(addr, size);
}

char *
symlink_cache_alloc(size_t size, int flags)
{
	char *rc;

	rc = (char *) kmem_alloc(size, flags);
	if (rc != NULL) {
		mutex_enter(&nfs_accurate_stats);
		clstat.symlink.value.ul++;
		mutex_exit(&nfs_accurate_stats);
	}
	return (rc);
}

void
symlink_cache_free(void *addr, size_t size)
{

	mutex_enter(&nfs_accurate_stats);
	clstat.symlink.value.ul--;
	mutex_exit(&nfs_accurate_stats);
	kmem_free(addr, size);
}
#endif

static int
nfs_free_data_reclaim(rnode_t *rp)
{
	access_cache *acp, *nacp;
	rddir_cache *rdc, *nrdc;
	char *contents;
	int size;
	vsecattr_t *vsp;

	/*
	 * Free any held credentials and caches which
	 * may be associated with this rnode.
	 */
	if (!mutex_tryenter(&rp->r_statelock))
		return (0);
	acp = rp->r_acc;
	rp->r_acc = NULL;
	rdc = rp->r_dir;
	rp->r_dir = NULL;
	rp->r_direof = NULL;
	contents = rp->r_symlink.contents;
	size = rp->r_symlink.size;
	rp->r_symlink.contents = NULL;
	vsp = rp->r_secattr;
	rp->r_secattr = NULL;
	mutex_exit(&rp->r_statelock);

	if (acp == NULL &&
	    rdc == NULL &&
	    contents == NULL &&
	    vsp == NULL)
		return (0);

	/*
	 * Free the access cache entries.
	 */
	while (acp != NULL) {
		crfree(acp->cred);
		nacp = acp->next;
#ifdef DEBUG
		access_cache_free((void *)acp, sizeof (*acp));
#else
		kmem_free((caddr_t)acp, sizeof (*acp));
#endif
		acp = nacp;
	}

	/*
	 * Free the readdir cache entries.
	 */
	while (rdc != NULL) {
		nrdc = rdc->next;
		mutex_enter(&rp->r_statelock);
		while (rdc->flags & RDDIR) {
			rdc->flags |= RDDIRWAIT;
			cv_wait(&rdc->cv, &rp->r_statelock);
		}
		mutex_exit(&rp->r_statelock);
		if (rdc->entries != NULL)
			kmem_free(rdc->entries, rdc->buflen);
		cv_destroy(&rdc->cv);
#ifdef DEBUG
		rddir_cache_free((void *)rdc, sizeof (*rdc));
#else
		kmem_free((caddr_t)rdc, sizeof (*rdc));
#endif
		rdc = nrdc;
	}

	/*
	 * Free the symbolic link cache.
	 */
	if (contents != NULL) {
#ifdef DEBUG
		symlink_cache_free((void *)contents, size);
#else
		kmem_free(contents, size);
#endif
	}

	/*
	 * Free any cached ACL.
	 */
	if (vsp != NULL)
		nfs_acl_free(vsp);

	return (1);
}

static int
nfs_active_data_reclaim(rnode_t *rp)
{
	access_cache *acp, *nacp;
	char *contents;
	int size;
	vsecattr_t *vsp;

	/*
	 * Free any held credentials and caches which
	 * may be associated with this rnode.
	 */
	if (!mutex_tryenter(&rp->r_statelock))
		return (0);
	acp = rp->r_acc;
	rp->r_acc = NULL;
	contents = rp->r_symlink.contents;
	size = rp->r_symlink.size;
	rp->r_symlink.contents = NULL;
	vsp = rp->r_secattr;
	rp->r_secattr = NULL;
	mutex_exit(&rp->r_statelock);

	if (acp == NULL &&
	    contents == NULL &&
	    vsp == NULL &&
	    rp->r_dir == NULL)
		return (0);

	/*
	 * Free the access cache entries.
	 */
	while (acp != NULL) {
		crfree(acp->cred);
		nacp = acp->next;
#ifdef DEBUG
		access_cache_free((void *)acp, sizeof (*acp));
#else
		kmem_free((caddr_t)acp, sizeof (*acp));
#endif
		acp = nacp;
	}

	/*
	 * Free the symbolic link cache.
	 */
	if (contents != NULL) {
#ifdef DEBUG
		symlink_cache_free((void *)contents, size);
#else
		kmem_free(contents, size);
#endif
	}

	/*
	 * Free any cached ACL.
	 */
	if (vsp != NULL)
		nfs_acl_free(vsp);

	if (rp->r_dir != NULL)
		nfs_purge_rddir_cache(RTOV(rp));

	return (1);
}

static int
nfs_free_reclaim(void)
{
	int freed;
	rnode_t *rp;

#ifdef DEBUG
	mutex_enter(&nfs_accurate_stats);
	clstat.f_reclaim.value.ul++;
	mutex_exit(&nfs_accurate_stats);
#endif
	freed = 0;
	mutex_enter(&nfs_rtable_lock);
	rp = rpfreelist;
	if (rp != NULL) {
		do {
			if (nfs_free_data_reclaim(rp))
				freed = 1;
		} while ((rp = rp->r_freef) != rpfreelist);
	}
	mutex_exit(&nfs_rtable_lock);
	return (freed);
}

static int
nfs_active_reclaim(void)
{
	int freed;
	rnode_t **rpp, **erpp, *rp;

#ifdef DEBUG
	mutex_enter(&nfs_accurate_stats);
	clstat.a_reclaim.value.ul++;
	mutex_exit(&nfs_accurate_stats);
#endif
	freed = 0;
	erpp = &rtable[rtablesize];
	mutex_enter(&nfs_rtable_lock);
	for (rpp = rtable; rpp < erpp; rpp++) {
		for (rp = *rpp; rp != NULL; rp = rp->r_hash) {
			if (nfs_active_data_reclaim(rp))
				freed = 1;
		}
	}
	mutex_exit(&nfs_rtable_lock);
	return (freed);
}

static int
nfs_rnode_reclaim(void)
{
	int freed;
	rnode_t *rp;
	vnode_t *vp;

#ifdef DEBUG
	mutex_enter(&nfs_accurate_stats);
	clstat.r_reclaim.value.ul++;
	mutex_exit(&nfs_accurate_stats);
#endif
	mutex_enter(&nfs_rtable_lock);
	if (rpfreelist == NULL) {
		mutex_exit(&nfs_rtable_lock);
		return (0);
	}
	freed = 0;
	while ((rp = rpfreelist) != NULL) {
		vp = RTOV(rp);
		rp_rmfree(rp);
		if (rp->r_flags & RHASHED)
			rp_rmhash(rp);
		mutex_exit(&nfs_rtable_lock);
		rinactive(rp, CRED());
		mutex_enter(&nfs_rtable_lock);
		if (vp->v_count == 0) {
			rw_destroy(&rp->r_rwlock);
			mutex_destroy(&rp->r_statelock);
			cv_destroy(&rp->r_cv);
			cv_destroy(&rp->r_commit.c_cv);
			mutex_destroy(&vp->v_lock);
			cv_destroy(&vp->v_cv);
			rnew--;
#ifdef DEBUG
			clstat.nrnode.value.ul--;
#endif
			kmem_cache_free(rnode_cache, (void *)rp);
			freed = 1;
		}
	}
	mutex_exit(&nfs_rtable_lock);
	return (freed);
}

static void
nfs_reclaim(void)
{

#ifdef DEBUG
	mutex_enter(&nfs_accurate_stats);
	clstat.reclaim.value.ul++;
	mutex_exit(&nfs_accurate_stats);
#endif
	if (nfs_free_reclaim())
		return;

	if (nfs_active_reclaim())
		return;

	(void) nfs_rnode_reclaim();
}

void
purge_authtab(mntinfo_t *mi)
{
	register struct desauthent *da;
	register struct kerbauthent *ka;

	switch (mi->mi_authflavor) {

		case AUTH_DES:
			mutex_enter(&desauthtab_lock);
			for (da = desauthtab;
			    da < &desauthtab[MAXCLIENTS]; da++) {
				if (da->da_mi == mi)
					da->da_mi = NULL;
			}
			mutex_exit(&desauthtab_lock);
			return;

		case AUTH_KERB:
			mutex_enter(&kerbauthtab_lock);
			for (ka = kerbauthtab;
			    ka < &kerbauthtab[MAXCLIENTS]; ka++) {
				if (ka->ka_mi == mi)
					ka->ka_mi = NULL;
			}
			mutex_exit(&kerbauthtab_lock);
			return;

		default:
			return;
	}
}
