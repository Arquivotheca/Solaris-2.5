/*
 * Copyright (c) 1986 - 1991, 1994 by Sun Microsystems, Inc.
 */

/*
 * clnt.h - Client side remote procedure call interface.
 *
 */

#ifndef	_RPC_CLNT_H
#define	_RPC_CLNT_H

#pragma ident	"@(#)clnt.h	1.36	95/02/06 SMI"

/* derived from clnt.h 1.53 89/05/01 SMI */

#include <rpc/rpc_com.h>
/*
 * rpc calls return an enum clnt_stat.  This should be looked at more,
 * since each implementation is required to live with this (implementation
 * independent) list of errors.
 */
#include <sys/netconfig.h>
#ifdef _KERNEL
#include <sys/t_kuser.h>
#endif	/* _KERNEL */

#ifdef __cplusplus
extern "C" {
#endif

enum clnt_stat {
	RPC_SUCCESS = 0,			/* call succeeded */
	/*
	 * local errors
	 */
	RPC_CANTENCODEARGS = 1,		/* can't encode arguments */
	RPC_CANTDECODERES = 2,		/* can't decode results */
	RPC_CANTSEND = 3,			/* failure in sending call */
	RPC_CANTRECV = 4,
	/* failure in receiving result */
	RPC_TIMEDOUT = 5,			/* call timed out */
	RPC_INTR = 18,			/* call interrupted */
	RPC_UDERROR = 23,			/* recv got uderr indication */
	/*
	 * remote errors
	 */
	RPC_VERSMISMATCH = 6,		/* rpc versions not compatible */
	RPC_AUTHERROR = 7,		/* authentication error */
	RPC_PROGUNAVAIL = 8,		/* program not available */
	RPC_PROGVERSMISMATCH = 9,		/* program version mismatched */
	RPC_PROCUNAVAIL = 10,		/* procedure unavailable */
	RPC_CANTDECODEARGS = 11,		/* decode arguments error */
	RPC_SYSTEMERROR = 12,		/* generic "other problem" */

	/*
	 * rpc_call & clnt_create errors
	 */
	RPC_UNKNOWNHOST = 13,		/* unknown host name */
	RPC_UNKNOWNPROTO = 17,		/* unknown protocol */
	RPC_UNKNOWNADDR = 19,		/* Remote address unknown */
	RPC_NOBROADCAST = 21,		/* Broadcasting not supported */

	/*
	 * rpcbind errors
	 */
	RPC_RPCBFAILURE = 14,		/* the pmapper failed in its call */
#define	RPC_PMAPFAILURE RPC_RPCBFAILURE
	RPC_PROGNOTREGISTERED = 15,	/* remote program is not registered */
	RPC_N2AXLATEFAILURE = 22,
	/* Name to address translation failed */
	/*
	 * Misc error in the TLI library
	 */
	RPC_TLIERROR = 20,
	/*
	 * unspecified error
	 */
	RPC_FAILED = 16,
	/*
	 * asynchronous errors
	 */
	RPC_INPROGRESS = 24,
	RPC_STALERACHANDLE = 25,
	RPC_CANTCONNECT = 26,		/* couldn't make connection (cots) */
	RPC_XPRTFAILED = 27,		/* received discon from remote (cots) */
	RPC_CANTCREATESTREAM = 28	/* can't push rpc module (cots) */
};


/*
 * Error info.
 */
struct rpc_err {
	enum clnt_stat re_status;
	union {
		struct {
			int RE_errno;	/* related system error */
			int RE_t_errno;	/* related tli error number */
		} RE_err;
		enum auth_stat RE_why;	/* why the auth error occurred */
		struct {
			u_long low;	/* lowest verion supported */
			u_long high;	/* highest verion supported */
		} RE_vers;
		struct {		/* maybe meaningful if RPC_FAILED */
			long s1;
			long s2;
		} RE_lb;		/* life boot & debugging only */
	} ru;
#define	re_errno	ru.RE_err.RE_errno
#define	re_terrno	ru.RE_err.RE_t_errno
#define	re_why		ru.RE_why
#define	re_vers		ru.RE_vers
#define	re_lb		ru.RE_lb
};


/*
 * Timers used for the pseudo-transport protocol when using datagrams
 */
struct rpc_timers {
	u_short		rt_srtt;	/* smoothed round-trip time */
	u_short		rt_deviate;	/* estimated deviation */
	u_long		rt_rtxcur;	/* current (backed-off) rto */
};

/*
 * Client rpc handle.
 * Created by individual implementations
 * Client is responsible for initializing auth, see e.g. auth_none.c.
 */

typedef struct __client {
	AUTH	*cl_auth;			/* authenticator */
	struct clnt_ops {
#ifdef __STDC__
		enum clnt_stat	(*cl_call)(struct __client *, u_long,
						xdrproc_t, caddr_t, xdrproc_t,
						caddr_t, struct timeval);
		/* call remote procedure */
		void		(*cl_abort)();	/* abort a call */
		void		(*cl_geterr)(struct __client *,
						struct rpc_err *);
		/* get specific error code */
		bool_t		(*cl_freeres)(struct __client *, xdrproc_t,
						caddr_t); /* frees results */
		void		(*cl_destroy)(struct __client *);
		/* destroy this structure */
		bool_t		(*cl_control)(struct __client *, int,
						char *);
		/* the ioctl() of rpc */
		int		(*cl_settimers)(struct __client *,
				struct rpc_timers *, struct rpc_timers *, int,
				void (*)(), caddr_t, u_long);
		/* set rpc level timers */
#else
		enum clnt_stat	(*cl_call)();	/* call remote procedure */
		void		(*cl_abort)();	/* abort a call */
		void		(*cl_geterr)();	/* get specific error code */
		bool_t		(*cl_freeres)(); /* frees results */
		void		(*cl_destroy)(); /* destroy this structure */
		bool_t		(*cl_control)(); /* the ioctl() of rpc */
		int		(*cl_settimers)(); /* set rpc level timers */
#endif
	} *cl_ops;
	caddr_t			cl_private;	/* private stuff */
#ifndef _KERNEL
	char			*cl_netid;	/* network token */
	char			*cl_tp;		/* device name */
#else
	kcondvar_t 		cl_cv;		/* CLIENT condition var */
#endif
} CLIENT;

/*
 * Feedback values used for possible congestion and rate control
 */
#define	FEEDBACK_REXMIT1	1	/* first retransmit */
#define	FEEDBACK_OK		2	/* no retransmits */

/* Used to set version of portmapper used in broadcast */

#define	CLCR_SET_LOWVERS	3
#define	CLCR_GET_LOWVERS	4

#define	RPCSMALLMSGSIZE	400	/* a more reasonable packet size */

#define	KNC_STRSIZE	128	/* maximum length of knetconfig strings */
/*
 * Note that the knetconfig strings can either be dynamically allocated, or
 * they can be string literals.  The code that sets up the knetconfig is
 * responsible for keeping track of this and freeing the strings if
 * necessary when the knetconfig is destroyed.
 */
struct knetconfig {
	unsigned long	knc_semantics;	/* token name */
	char		*knc_protofmly;	/* protocol family */
	char		*knc_proto;	/* protocol */
	dev_t		knc_rdev;	/* device id */
	unsigned long	knc_unused[8];
};

#ifdef _KERNEL
/*
 *	Alloc_xid presents an interface which kernel RPC clients
 *	should use to allocate their XIDs.  Its implementation
 *	may change over time (for example, to allow sharing of
 *	XIDs between the kernel and user-level applications, so
 *	all XID allocation should be done by calling alloc_xid().
 */
extern u_long alloc_xid(void);

extern int clnt_tli_kcreate(struct knetconfig *config, struct netbuf *svcaddr,
	u_long prog, u_long vers, u_int max_msgsize, int retrys,
	struct cred *cred, CLIENT **ncl);

extern int clnt_tli_kinit(CLIENT *h, struct knetconfig *config,
	struct netbuf *addr, u_int max_msgsize, int retries, struct cred *cred);

extern int rpc_uaddr2port(char *addr);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern int bindresvport(TIUSER *tiptr, struct netbuf *addr,
	struct netbuf *bound_addr, bool_t istcp);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern int clnt_clts_kcreate(TIUSER *tiptr, dev_t rdev, struct netbuf *addr,
	u_long prog, u_long vers, int retries, struct cred *cred, CLIENT **cl);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern int clnt_cots_kcreate(dev_t dev, struct netbuf *addr, int family,
	u_long prog, u_long vers, u_int max_msgsize, struct cred *cred,
	CLIENT **ncl);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern void clnt_clts_kinit(CLIENT *h, struct netbuf *addr, int retries,
	struct cred *cred);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern void clnt_cots_kinit(CLIENT *h, dev_t dev, int family,
	struct netbuf *addr, int max_msgsize, struct cred *cred);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern bool_t clnt_dispatch_notify(mblk_t *);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern bool_t clnt_dispatch_notifyconn(queue_t *, mblk_t *);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern void clnt_dispatch_notifyall(queue_t *, long);

/*
 * kRPC internal function. Not for general use. Subject to rapid change.
 */
extern enum clnt_stat clnt_clts_kcallit_addr(CLIENT *, u_long, xdrproc_t,
	caddr_t, xdrproc_t, caddr_t, struct timeval, struct netbuf *);

#endif /* _KERNEL */

/*
 * client side rpc interface ops
 */

/*
 * enum clnt_stat
 * CLNT_CALL(rh, proc, xargs, argsp, xres, resp, timeout)
 * 	CLIENT *rh;
 *	u_long proc;
 *	xdrproc_t xargs;
 *	caddr_t argsp;
 *	xdrproc_t xres;
 *	caddr_t resp;
 *	struct timeval timeout;
 */
#define	CLNT_CALL(rh, proc, xargs, argsp, xres, resp, secs)	\
	((*(rh)->cl_ops->cl_call)(rh, proc, xargs, argsp, xres, resp, secs))
#define	clnt_call(rh, proc, xargs, argsp, xres, resp, secs)	\
	((*(rh)->cl_ops->cl_call)(rh, proc, xargs, argsp, xres, resp, secs))

/*
 * void
 * CLNT_ABORT(rh);
 * 	CLIENT *rh;
 */
#define	CLNT_ABORT(rh)	((*(rh)->cl_ops->cl_abort)(rh))
#define	clnt_abort(rh)	((*(rh)->cl_ops->cl_abort)(rh))

/*
 * struct rpc_err
 * CLNT_GETERR(rh);
 * 	CLIENT *rh;
 */
#define	CLNT_GETERR(rh, errp)	((*(rh)->cl_ops->cl_geterr)(rh, errp))
#define	clnt_geterr(rh, errp)	((*(rh)->cl_ops->cl_geterr)(rh, errp))

/*
 * bool_t
 * CLNT_FREERES(rh, xres, resp);
 * 	CLIENT *rh;
 *	xdrproc_t xres;
 *	caddr_t resp;
 */
#define	CLNT_FREERES(rh, xres, resp) \
		((*(rh)->cl_ops->cl_freeres)(rh, xres, resp))
#define	clnt_freeres(rh, xres, resp) \
			((*(rh)->cl_ops->cl_freeres)(rh, xres, resp))

/*
 * bool_t
 * CLNT_CONTROL(cl, request, info)
 *	CLIENT *cl;
 *	u_int request;
 *	char *info;
 */
#define	CLNT_CONTROL(cl, rq, in) ((*(cl)->cl_ops->cl_control)(cl, rq, in))
#define	clnt_control(cl, rq, in) ((*(cl)->cl_ops->cl_control)(cl, rq, in))


/*
 * control operations that apply to all transports
 */
#define	CLSET_TIMEOUT		1	/* set timeout (timeval) */
#define	CLGET_TIMEOUT		2	/* get timeout (timeval) */
#define	CLGET_SERVER_ADDR	3	/* get server's address (sockaddr) */
#define	CLGET_FD		6	/* get connections file descriptor */
#define	CLGET_SVC_ADDR		7	/* get server's address (netbuf) */
#define	CLSET_FD_CLOSE		8	/* close fd while clnt_destroy */
#define	CLSET_FD_NCLOSE		9	/* Do not close fd while clnt_destroy */
#define	CLGET_XID 		10	/* Get xid */
#define	CLSET_XID		11	/* Set xid */
#define	CLGET_VERS		12	/* Get version number */
#define	CLSET_VERS		13	/* Set version number */
#define	CLGET_PROG		14	/* Get program number */
#define	CLSET_PROG		15	/* Set program number */
#define	CLSET_SVC_ADDR		16	/* get server's address (netbuf) */
#define	CLSET_PUSH_TIMOD	17	/* push timod if not already present */
#define	CLSET_POP_TIMOD		18	/* pop timod */
/*
 * Connectionless only control operations
 */
#define	CLSET_RETRY_TIMEOUT 4   /* set retry timeout (timeval) */
#define	CLGET_RETRY_TIMEOUT 5   /* get retry timeout (timeval) */

/*
 * void
 * CLNT_SETTIMERS(rh);
 *	CLIENT *rh;
 *	struct rpc_timers *t;
 *	struct rpc_timers *all;
 *	unsigned int min;
 *	void    (*fdbck)();
 *	caddr_t arg;
 *	u_long  xid;
 */
#define	CLNT_SETTIMERS(rh, t, all, min, fdbck, arg, xid) \
		((*(rh)->cl_ops->cl_settimers)(rh, t, all, min, \
		fdbck, arg, xid))
#define	clnt_settimers(rh, t, all, min, fdbck, arg, xid) \
		((*(rh)->cl_ops->cl_settimers)(rh, t, all, min, \
		fdbck, arg, xid))


/*
 * void
 * CLNT_DESTROY(rh);
 * 	CLIENT *rh;
 */
#define	CLNT_DESTROY(rh)	((*(rh)->cl_ops->cl_destroy)(rh))
#define	clnt_destroy(rh)	((*(rh)->cl_ops->cl_destroy)(rh))


/*
 * RPCTEST is a test program which is accessable on every rpc
 * transport/port.  It is used for testing, performance evaluation,
 * and network administration.
 */

#define	RPCTEST_PROGRAM		((u_long)1)
#define	RPCTEST_VERSION		((u_long)1)
#define	RPCTEST_NULL_PROC	((u_long)2)
#define	RPCTEST_NULL_BATCH_PROC	((u_long)3)

/*
 * By convention, procedure 0 takes null arguments and returns them
 */

#define	NULLPROC ((u_long)0)

/*
 * Below are the client handle creation routines for the various
 * implementations of client side rpc.  They can return NULL if a
 * creation failure occurs.
 */

#ifndef _KERNEL

/*
 * Generic client creation routine. Supported protocols are which belong
 * to the nettype name space
 */
#ifdef __STDC__
extern  CLIENT * clnt_create(const char *, const u_long, const u_long, const
char *);
/*
 *
 * 	const char *hostname;			-- hostname
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 *	const char *nettype;			-- network type
 */
#else
extern CLIENT * clnt_create();
#endif

/*
 * Generic client creation routine. Just like clnt_create(), except
 * it takes an additional timeout parameter.
 */
#ifdef __STDC__
extern  CLIENT * clnt_create_timed(const char *, const u_long, const u_long,
const char *, const struct timeval *);
/*
 *
 * 	const char *hostname;			-- hostname
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 *	const char *nettype;			-- network type
 *	const struct timeval *tp;		-- timeout
 */
#else
extern CLIENT * clnt_create_timed();
#endif

/*
 * Generic client creation routine. Supported protocols are which belong
 * to the nettype name space.
 */
#ifdef __STDC__
extern CLIENT * clnt_create_vers(const char *, const u_long, u_long *, const
u_long,	const u_long, const char *);
/*
 *	const char *host;		-- hostname
 *	const u_long prog;		-- program number
 *	u_long *vers_out;	-- servers highest available version number
 *	const u_long vers_low;	-- low version number
 *	const u_long vers_high;	-- high version number
 *	const char *nettype;		-- network type
 */
#else
extern CLIENT * clnt_create_vers();
#endif


/*
 * Generic client creation routine. It takes a netconfig structure
 * instead of nettype
 */
#ifdef __STDC__
extern CLIENT * clnt_tp_create(const char *, const u_long, const u_long, const
struct netconfig *);
/*
 *	const char *hostname;			-- hostname
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 *	const struct netconfig *netconf; 	-- network config structure
 */
#else
extern CLIENT * clnt_tp_create();
#endif

/*
 * Generic client creation routine. Just like clnt_tp_create(), except
 * it takes an additional timeout parameter.
 */
#ifdef __STDC__
extern CLIENT * clnt_tp_create_timed(const char *, const u_long, const u_long,
const struct netconfig *, const struct timeval *);
/*
 *	const char *hostname;			-- hostname
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 *	const struct netconfig *netconf; 	-- network config structure
 *	const struct timeval *tp;		-- timeout
 */
#else
extern CLIENT * clnt_tp_create_timed();
#endif

/*
 * Generic TLI create routine
 */

#ifdef __STDC__
extern CLIENT * clnt_tli_create(const int, const struct netconfig *, const
struct netbuf *, const u_long, const u_long, const u_int, const u_int);
/*
 *	const int fd;		-- fd
 *	const struct netconfig *nconf;	-- netconfig structure
 *	const struct netbuf *svcaddr;		-- servers address
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 *	const u_int sendsz;			-- send size
 *	const u_int recvsz;			-- recv size
 */

#else
extern CLIENT * clnt_tli_create();
#endif

/*
 * Low level clnt create routine for connectionful transports, e.g. tcp.
 */
#ifdef __STDC__
extern  CLIENT * clnt_vc_create(const int, const struct netbuf *, const u_long,
const u_long, const u_int, const u_int);
/*
 *	const int fd;				-- open file descriptor
 *	const struct netbuf *svcaddr;		-- servers address
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 *	const u_int sendsz;			-- buffer recv size
 *	const u_int recvsz;			-- buffer send size
 */
#else
extern CLIENT * clnt_vc_create();
#endif

/*
 * Low level clnt create routine for connectionless transports, e.g. udp.
 */
#ifdef __STDC__
extern  CLIENT * clnt_dg_create(const int, const struct netbuf *, const u_long,
const u_long, const u_int, const u_int);
/*
 *	const int fd;				-- open file descriptor
 *	const struct netbuf *svcaddr;		-- servers address
 *	const u_long program;			-- program number
 *	const u_long version;			-- version number
 *	const u_int sendsz;			-- buffer recv size
 *	const u_int recvsz;			-- buffer send size
 */
#else
extern CLIENT * clnt_dg_create();
#endif

/*
 * Memory based rpc (for speed check and testing)
 * CLIENT *
 * clnt_raw_create(prog, vers)
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 */
#ifdef __STDC__
extern CLIENT *clnt_raw_create(const u_long, const u_long);
#else
extern CLIENT *clnt_raw_create();
#endif


/*
 * Print why creation failed
 */
#ifdef __STDC__
void clnt_pcreateerror(const char *);	/* stderr */
char *clnt_spcreateerror(const char *);	/* string */
#else
void clnt_pcreateerror();
char *clnt_spcreateerror();
#endif

/*
 * Like clnt_perror(), but is more verbose in its output
 */
#ifdef __STDC__
void clnt_perrno(const enum clnt_stat);	/* stderr */
#else
void clnt_perrno();
#endif

/*
 * Print an error message, given the client error code
 */
#ifdef __STDC__
void clnt_perror(const CLIENT *, const char *);
/* stderr */
char *clnt_sperror(const CLIENT *, const char *);
/* string */
#else
void clnt_perror();
char *clnt_sperror();
#endif

/*
 * If a creation fails, the following allows the user to figure out why.
 */
struct rpc_createerr {
	enum clnt_stat cf_stat;
	struct rpc_err cf_error; /* useful when cf_stat == RPC_PMAPFAILURE */
};

#ifdef	_REENTRANT
extern struct rpc_createerr	*__rpc_createerr();
#define	rpc_createerr	(*(__rpc_createerr()))
#else
extern struct rpc_createerr rpc_createerr;
#endif	/* _REENTRANT */

/*
 * The simplified interface:
 * enum clnt_stat
 * rpc_call(host, prognum, versnum, procnum, inproc, in, outproc, out, nettype)
 *	const char *host;
 *	const u_long prognum, versnum, procnum;
 *	const xdrproc_t inproc, outproc;
 *	const char *in;
 *	char *out;
 *	const char *nettype;
 */
#ifdef __STDC__
extern enum clnt_stat rpc_call(const char *, const u_long, const u_long, const
u_long, const xdrproc_t, const char *, const xdrproc_t, char *, const char *);
#else
extern enum clnt_stat rpc_call();
#endif

/*
 * RPC broadcast interface
 * The call is broadcasted to all locally connected nets.
 *
 * extern enum clnt_stat
 * rpc_broadcast(prog, vers, proc, xargs, argsp, xresults, resultsp,
 *			eachresult, nettype)
 *	const u_long		prog;		-- program number
 *	const u_long		vers;		-- version number
 *	const u_long		proc;		-- procedure number
 *	const xdrproc_t	xargs;		-- xdr routine for args
 *	caddr_t		argsp;		-- pointer to args
 *	const xdrproc_t	xresults;	-- xdr routine for results
 *	caddr_t		resultsp;	-- pointer to results
 *	const resultproc_t	eachresult;	-- call with each result
 *	const char		*nettype;	-- Transport type
 *
 * For each valid response received, the procedure eachresult is called.
 * Its form is:
 *		done = eachresult(resp, raddr, nconf)
 *			bool_t done;
 *			caddr_t resp;
 *			struct netbuf *raddr;
 *			struct netconfig *nconf;
 * where resp points to the results of the call and raddr is the
 * address if the responder to the broadcast.  nconf is the transport
 * on which the response was received.
 *
 * extern enum clnt_stat
 * rpc_broadcast_exp(prog, vers, proc, xargs, argsp, xresults, resultsp,
 *			eachresult, inittime, waittime, nettype)
 *	const u_long		prog;		-- program number
 *	const u_long		vers;		-- version number
 *	const u_long		proc;		-- procedure number
 *	const xdrproc_t	xargs;		-- xdr routine for args
 *	caddr_t		argsp;		-- pointer to args
 *	const xdrproc_t	xresults;	-- xdr routine for results
 *	caddr_t		resultsp;	-- pointer to results
 *	const resultproc_t	eachresult;	-- call with each result
 *	const int 		inittime;	-- how long to wait initially
 *	const int 		waittime;	-- maximum time to wait
 *	const char		*nettype;	-- Transport type
 */

typedef bool_t(*resultproc_t)(
#ifdef	__STDC__
	caddr_t,
	... /* for backward compatibility */
#endif				/* __STDC__ */
);
#ifdef __STDC__
extern enum clnt_stat rpc_broadcast(const u_long, const u_long, const u_long,
const xdrproc_t, caddr_t, const xdrproc_t, caddr_t, const resultproc_t, const
char *);
extern enum clnt_stat rpc_broadcast_exp(const u_long, const u_long, const
u_long, const xdrproc_t, caddr_t, const xdrproc_t, caddr_t, const resultproc_t,
const int, const int, const char *);
#else
extern enum clnt_stat rpc_broadcast();
extern enum clnt_stat rpc_broadcast_exp();
#endif
#endif /* !_KERNEL */

/*
 * Copy error message to buffer.
 */
#ifdef __STDC__
char *clnt_sperrno(const enum clnt_stat /* enum clnt_stat num */);
#else
char *clnt_sperrno();	/* string */
#endif

#ifdef _KERNEL
extern void rpc_revoke_key(AUTH *, int);
#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#ifdef PORTMAP
/* For backward compatibility */
#include <rpc/clnt_soc.h>
#endif

#endif	/* !_RPC_CLNT_H */
