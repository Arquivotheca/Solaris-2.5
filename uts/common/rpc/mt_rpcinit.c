/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mt_rpcinit.c	1.5	94/10/19 SMI"	/* SVr4.0	*/

/*
 * This file is a merge of the previous two separate files: mt_clntinit.c,
 * mt_svcinit.c, plus some kstat_create code from os/kstat_fr.c file.
 * Previously, mt_rpcclnt_init() and mt_rpcsvc_init() are called from
 * startup() routine in $KARCH/os/startup.c; and mt_kstat_init() is
 * called from os/kstat_fr.c. Now, all three of them are called from
 * the _init() routine in rpcmod.c.
 */

/*
 *	Define and initialize MT client/server data.
 */
#include	<sys/types.h>
#include	<sys/t_lock.h>
#include	<sys/kstat.h>

static struct rwlock_init_table {
	krwlock_t	*addr;
	void		*weight;
	char		*name;
	krw_type_t	type;

};
static struct mutex_init_table {
	kmutex_t	*addr;
	void		*weight;
	char		*name;
	kmutex_type_t	type;
};
kmutex_t authdes_ops_lock;	/* auth_ops initialization in authdes_ops() */
kmutex_t authkerb_ops_lock;	/* auth_ops initialization in authkerb_ops() */
kmutex_t rcstat_lock;		/* rcstat structure updating */
kmutex_t xid_lock;		/* XID allocation */
kmutex_t clnt_pending_lock;	/* for list of pending calls awaiting replies */
kmutex_t connmgr_lock;		/* for connection mngr's list of transports */
kmutex_t clnt_max_msg_lock;	/* updating max message sanity check for cots */

static struct mutex_init_table rpcclnt_mutex_init_table[] = {
	&authdes_ops_lock,	DEFAULT_WT,	"auth_ops", 	MUTEX_DEFAULT,
	&authkerb_ops_lock,	DEFAULT_WT,	"auth_ops", 	MUTEX_DEFAULT,
	&rcstat_lock,		DEFAULT_WT,	"rcstat", 	MUTEX_DEFAULT,
	&xid_lock,		DEFAULT_WT,	"XID allocation", MUTEX_DEFAULT,
	&clnt_pending_lock,	DEFAULT_WT,	"pending rpcs",	MUTEX_DEFAULT,
	&connmgr_lock,		DEFAULT_WT,	"rpc transports", MUTEX_DEFAULT,
	&clnt_max_msg_lock,	DEFAULT_WT,	"cots msg size", MUTEX_DEFAULT,
	(kmutex_t *) 0,		0,		(char *) 0,	MUTEX_DEFAULT
};

/* protects the authdes cache (svcauthdes.c) */
kmutex_t	authdes_lock;
/* protects the authkerb cache (svcauthkerb.c) */
kmutex_t	authkerb_lock;
/* protects the dupreq variables (svc_clts.c) */
kmutex_t	dupreq_lock;
/* protects the connection-oriented dupreq variables (svc_cots.c) */
kmutex_t	cotsdupreq_lock;
/* protects the request cred list (svc.c) */
kmutex_t	rqcred_lock;

/* protects the service request statistics structure (svc_clts.c) */
kmutex_t	rsstat_lock;
/* protects the svcauthdes_stats structure (svcauthdes.c) */
kmutex_t	svcauthdesstats_lock;
/* protects the svcauthkerb_stats structure (svcauthkerb.c) */
kmutex_t	svcauthkerbstats_lock;

/* protects the services list (svc.c) */
krwlock_t	svc_lock;
/* protects the transports list (svc.c) */
krwlock_t	xprt_lock;

static struct rwlock_init_table rpcsvc_rwlock_init_table[] = {
	&svc_lock,	DEFAULT_WT,	"services list",	RW_DEFAULT,
	&xprt_lock,	DEFAULT_WT,	"transports list",	RW_DEFAULT,
	(krwlock_t *)0,	0,		(char *)0,		RW_DEFAULT
};
static struct mutex_init_table rpcsvc_mutex_init_table[] = {
	&authdes_lock,	DEFAULT_WT,	"authdes cache",	MUTEX_DEFAULT,
	&authkerb_lock,	DEFAULT_WT,	"authkerb cache",	MUTEX_DEFAULT,
	&dupreq_lock,	DEFAULT_WT,	"dupreq",		MUTEX_DEFAULT,
	&rqcred_lock,	DEFAULT_WT,	"request cred list",	MUTEX_DEFAULT,
	&cotsdupreq_lock, DEFAULT_WT,	"cotsdupreq",		MUTEX_DEFAULT,
	&rsstat_lock,	DEFAULT_WT,	"service statistics",	MUTEX_DEFAULT,
	&svcauthdesstats_lock, DEFAULT_WT, "svcauthdes_stats", 	MUTEX_DEFAULT,
	&svcauthkerbstats_lock, DEFAULT_WT, "svcauthkerb_stats", MUTEX_DEFAULT,
	(kmutex_t *)0,	0,		(char *)0,		MUTEX_DEFAULT
};

extern	kstat_named_t	*rcstat_ptr;
extern	ulong_t		rcstat_ndata;
extern	kstat_named_t	*rsstat_ptr;
extern	ulong_t		rsstat_ndata;
extern  kstat_named_t   *cotsrcstat_ptr;
extern	ulong_t		cotsrcstat_ndata;
extern	kstat_named_t	*cotsrsstat_ptr;
extern	ulong_t		cotsrsstat_ndata;

void
mt_rpcclnt_init()
{
	struct mutex_init_table	*mp;

	for (mp = rpcclnt_mutex_init_table; mp->addr != (kmutex_t *) 0; mp++)
		mutex_init(mp->addr, mp->name, mp->type, mp->weight);
}

void
mt_rpcsvc_init()
{
	struct mutex_init_table *mp;
	struct rwlock_init_table *rp;

	for (mp = rpcsvc_mutex_init_table; mp->addr != (kmutex_t *)0; mp++)
		mutex_init(mp->addr, mp->name, mp->type, mp->weight);

	for (rp = rpcsvc_rwlock_init_table; rp->addr != (krwlock_t *)0; rp++)
		rw_init(rp->addr, rp->name, rp->type, rp->weight);
}

void
mt_kstat_init()
{
	kstat_t *ksp;

	ksp = kstat_create("unix", 0, "rpc_clts_client", "rpc",
		KSTAT_TYPE_NAMED, rcstat_ndata,
		KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE);
	if (ksp) {
		ksp->ks_data = (void *) rcstat_ptr;
		kstat_install(ksp);
	}

	ksp = kstat_create("unix", 0, "rpc_cots_client", "rpc",
		KSTAT_TYPE_NAMED, cotsrcstat_ndata,
		KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE);
	if (ksp) {
		ksp->ks_data = (void *) cotsrcstat_ptr;
		kstat_install(ksp);
	}

	ksp = kstat_create("unix", 0, "rpc_cots_connections", "rpc",
		KSTAT_TYPE_NAMED, 0, KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_VAR_SIZE);
	if (ksp) {
		extern int conn_kstat_update(), conn_kstat_snapshot();

		ksp->ks_lock = &connmgr_lock;
		ksp->ks_update = conn_kstat_update;
		ksp->ks_snapshot = conn_kstat_snapshot;
		kstat_install(ksp);
	}

	/*
	 * Backwards compatibility for old kstat clients
	 */
	ksp = kstat_create("unix", 0, "rpc_client", "rpc",
		KSTAT_TYPE_NAMED, rcstat_ndata,
		KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE);
	if (ksp) {
		ksp->ks_data = (void *) rcstat_ptr;
		kstat_install(ksp);
	}

	ksp = kstat_create("unix", 0, "rpc_clts_server", "rpc",
		KSTAT_TYPE_NAMED, rsstat_ndata,
		KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE);
	if (ksp) {
		ksp->ks_data = (void *) rsstat_ptr;
		kstat_install(ksp);
	}

	ksp = kstat_create("unix", 0, "rpc_cots_server", "rpc",
		KSTAT_TYPE_NAMED, cotsrsstat_ndata,
		KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE);
	if (ksp) {
		ksp->ks_data = (void *) cotsrsstat_ptr;
		kstat_install(ksp);
	}

	/*
	 * Backwards compatibility for old kstat clients
	 */
	ksp = kstat_create("unix", 0, "rpc_server", "rpc",
		KSTAT_TYPE_NAMED, rsstat_ndata,
		KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE);
	if (ksp) {
		ksp->ks_data = (void *) rsstat_ptr;
		kstat_install(ksp);
	}
}

static int clnt_xid = 0;	/* transaction id used by all clients */

u_long
alloc_xid()
{
	u_long  xid;

	mutex_enter(&xid_lock);
	if (!clnt_xid) {
		clnt_xid = (u_long) ((hrestime.tv_sec << 20) |
						(hrestime.tv_nsec >> 10));
	}

	/*
	 * Pre-increment in-case for whatever reason, clnt_xid is still
	 * zero after the above initialization.
	 */
	xid = ++clnt_xid;
	mutex_exit(&xid_lock);
	return (xid);
}
