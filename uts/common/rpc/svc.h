/*
 * Copyright (c) 1986 - 1994 by Sun Microsystems, Inc.
 */

/*
 * svc.h, Server-side remote procedure call interface.
 *
 */

#ifndef _RPC_SVC_H
#define	_RPC_SVC_H

#pragma ident	"@(#)svc.h	1.35	95/01/13 SMI"

#include <rpc/rpc_com.h>
#include <rpc/rpc_msg.h>
#include <sys/tihdr.h>

#ifdef _KERNEL
#include <sys/callb.h>
#endif

/*
 * This interface must manage two items concerning remote procedure calling:
 *
 * 1) An arbitrary number of transport connections upon which rpc requests
 * are received. They are created and registered by routines in svc_generic.c,
 * svc_vc.c and svc_dg.c; they in turn call xprt_register and
 * xprt_unregister.
 *
 * 2) An arbitrary number of locally registered services.  Services are
 * described by the following four data: program number, version number,
 * "service dispatch" function, a transport handle, and a boolean that
 * indicates whether or not the exported program should be registered with a
 * local binder service;  if true the program's number and version and the
 * address from the transport handle are registered with the binder.
 * These data are registered with rpcbind via svc_reg().
 *
 * A service's dispatch function is called whenever an rpc request comes in
 * on a transport.  The request's program and version numbers must match
 * those of the registered service.  The dispatch function is passed two
 * parameters, struct svc_req * and SVCXPRT *, defined below.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 *	Service control requests
 */
#define	SVCGET_VERSQUIET	1
#define	SVCSET_VERSQUIET	2
#ifdef _KERNEL
#define	SVCSET_CLOSEPROC	3
#endif

enum xprt_stat {
	XPRT_DIED,
	XPRT_MOREREQS,
	XPRT_IDLE
};

/*
 * Service request
 */
struct svc_req {
	u_long		rq_prog;	/* service program number */
	u_long		rq_vers;	/* service protocol version */
	u_long		rq_proc;	/* the desired procedure */
	struct opaque_auth rq_cred;	/* raw creds from the wire */
	caddr_t		rq_clntcred;	/* read only cooked cred */
	struct __svcxprt *rq_xprt;	/* associated transport */
};

#ifdef _KERNEL
struct dupreq {
	u_long		dr_xid;
	u_long		dr_proc;
	u_long		dr_vers;
	u_long		dr_prog;
	struct netbuf	dr_addr;
	struct netbuf	dr_resp;
	int		dr_status;
	struct dupreq	*dr_next;
	struct dupreq	*dr_chain;
};

/*
 * states of requests for duplicate request caching
 */
#define	DUP_NEW			0x00	/* new entry */
#define	DUP_INPROGRESS		0x01	/* request already going */
#define	DUP_DONE		0x02	/* request done */
#define	DUP_DROP		0x03	/* request dropped */
#define	DUP_ERROR		0x04	/* error in dup req cache */
#endif

struct xp_ops {
#ifdef __STDC__
#ifdef _KERNEL
		bool_t	(*xp_recv)(struct __svcxprt *, mblk_t *,
			struct rpc_msg *); /* receive incoming requests */
#else
		bool_t	(*xp_recv)(struct __svcxprt *, struct rpc_msg *);
			/* receive incoming requests */
#endif
		enum xprt_stat (*xp_stat)(struct __svcxprt *);
			/* get transport status */
		bool_t	(*xp_getargs)(struct __svcxprt *, xdrproc_t,
					caddr_t); /* get arguments */
		bool_t	(*xp_reply)(struct __svcxprt *,
				    struct rpc_msg *); /* send reply */
		bool_t	(*xp_freeargs)(struct __svcxprt *, xdrproc_t,
					caddr_t);
			/* free mem allocated for args */
		void	(*xp_destroy)(struct __svcxprt *);
			/* destroy this struct */
#ifdef _KERNEL
		int	(*xp_dup)(struct svc_req *, caddr_t, int,
				struct dupreq **); /* check for dup */
		void	(*xp_dupdone)(struct dupreq *, caddr_t, int, int);
			/* mark dup entry as completed */
		long *	(*xp_getres)(struct __svcxprt *, int);
			/* get pointer to response buffer */
		void	(*xp_freeres)(struct __svcxprt *);
			/* destroy pre-serialized response */
		void	(*xp_clone)(struct __svcxprt *,
			struct __svcxprt *, caddr_t);
			/* clone a master xprt */
		void	(*xp_clone_destroy)(struct __svcxprt *);
			/* destroy a clone xprt */
#else
		bool_t	(*xp_control)(struct __svcxprt *, const u_int,
				void *); /* catch-all control function */
#endif
#else /* __STDC__ */
		bool_t	(*xp_recv)(); /* receive incoming requests */
		enum xprt_stat (*xp_stat)(); /* get transport status */
		bool_t	(*xp_getargs)(); /* get arguments */
		bool_t	(*xp_reply)(); /* send reply */
		bool_t	(*xp_freeargs)(); /* free mem allocated for args */
		void	(*xp_destroy)(); /* destroy this struct */
#ifdef _KERNEL
		int	(*xp_dup)(); /* check for dup */
		void	(*xp_dupdone)(); /* mark dup entry as completed */
		long *	(*xp_getres)();	/* get pointer to response buffer */
		void	(*xp_freeres)(); /* destroy pre-serialized response */
		void	(*xp_clone)();  /* clone a master xprt */
		int	(*xp_clone_destroy)();  /* destroy a master xprt */
#else
		bool_t	(*xp_control)(); /* catch-all control function */
#endif
#endif /* __STDC__ */
};

/*
 * Server side transport handle
 * xprt->xp_req_lock governs the following fields in xprt:
 *		xp_req_head, xp_req_tail, xp_asleep, and xp_drowsy.
 * xprt->xp_thread_lock governs the following fields in xprt:
 *		xp_max_threads, xp_min_threads, xp_detached_threads,
 *		xp_threads, and xp_dead_cv.
 * xprt->xp_lock governs the rest of the fields in xprt, except for the
 * 		clone-only fields, which are not locked.
 *
 * The xp_threads count is the number of attached threads.  These threads
 * are able to handle new requests, and it is expected that they will not
 * block for a very long time handling a given request.  The
 * xp_detached_threads count is the number of threads that have detached
 * themselves from the transport.  These threads can block indefinitely
 * while handling a request.  Once they complete the request, they exit.
 * If the number of attached threads goes to zero, the transport can be
 * closed.  If the sum of attached and detached threads goes to zero, the
 * data structure for the transport can be freed.
 *
 * A kernel service provider may register a callback function "closeproc"
 * for a transport.  When the last attached thread exits (xp_threads goes
 * to zero in svc_thread_exit) it calls the callback function, passing it
 * a reference to the transport.  This call is made with xp_thread_lock
 * held, so any cleanup bookkeeping it does should be done quickly.
 */

typedef struct __svcxprt {
#ifdef _KERNEL
	struct file	* xp_fp;
	struct xp_ops   * xp_ops;
	queue_t		*xp_wq;		/* queue to write onto */
	mblk_t		* xp_req_head;
	mblk_t		* xp_req_tail;
	kmutex_t	xp_req_lock;	/* Request lock */
	kcondvar_t	xp_req_cv;
	cred_t		* xp_cred;	/* cached cred for server to use */
	struct __svcxprt *xp_next;	/* list of available service handles */
	long		xp_type;	/* transport type */
	int		xp_msg_size;	/* TSDU or TIDU size from transport */
	struct netbuf	xp_rtaddr;	/* remote transport address */
	kmutex_t	xp_lock;	/* xprt structure lock */
	int		xp_max_threads;	/* Maximum total number of threads */
	int		xp_min_threads;	/* Minimum num. of attached threads */
	int		xp_threads;	/* Current num. of attached threads */
	int		xp_detached_threads; /* num. of detached threads */
	kmutex_t	xp_thread_lock;	/* Thread count lock */
	int		xp_asleep; 	/* Current number of asleep threads */
	int		xp_drowsy; 	/* Current number of drowsy threads */
	void		(*xp_closeproc)(const struct __svcxprt *);
					/* optional; see comments above */
	kcondvar_t	xp_dead_cv;	/* svc threads are all gone */

	/*
	 * The following fields are null for master xprt's.  They are filled
	 * in for clone xprt's.  Because a clone is used by only one
	 * thread, it is not necessary to lock these fields.
	 */
	struct __svcxprt *xp_master;	/* back ptr to master */
	callb_cpr_t	* xp_cprp;	/* thread's CPR info */
	bool_t		xp_detached;	/* is the clone's thread detached */

	/* The following fields are used on a per-request basis. */
	caddr_t		xp_p1;		/* private: for use by svc ops */
	caddr_t		xp_p2;		/* private: for use by svc ops */
	caddr_t		xp_p3;		/* private: for use by svc lib */
	struct opaque_auth xp_verf;	/* raw response verifier */
	u_long		xp_xid;		/* id */
	XDR		xp_xdrin;	/* input xdr stream */
	XDR		xp_xdrout;	/* output xdr stream */

#else	/* KERNEL */
	int		xp_fd;
#define	xp_sock		xp_fd
	u_short		xp_port;
	/*
	 * associated port number.
	 * Obsoleted, but still used to
	 * specify whether rendezvouser
	 * or normal connection
	 */
	struct	xp_ops	*xp_ops;
	int		xp_addrlen;	 /* length of remote addr. Obsoleted */
	char		*xp_tp;		 /* transport provider device name */
	char		*xp_netid;	 /* network token */
	struct netbuf	xp_ltaddr;	 /* local transport address */
	struct netbuf	xp_rtaddr;	 /* remote transport address */
	char		xp_raddr[16];	 /* remote address. Now obsoleted */
	struct opaque_auth xp_verf;	 /* raw response verifier */
	caddr_t		xp_p1;		 /* private: for use by svc ops */
	caddr_t		xp_p2;		 /* private: for use by svc ops */
	caddr_t		xp_p3;		 /* private: for use by svc lib */
	int		xp_type;	/* transport type */
#endif
} SVCXPRT;

#ifdef _KERNEL
/*
 * Maximum p1 and p2 buffer lengths for quick xprts.
 * These constants must
 * be changed if a new transport is added and requires more space.
 * Also note that these buffers are allocated on the stack and
 * shouldn't be very big.  A transport could dynamically allocate
 * a bigger structure on a per-call basis in its svc_x_clone routine
 * and free the memory in its svc_x_clone_destroy.
 */
#define	SVC_MAX_P1LEN   0
#define	SVC_MAX_P2LEN   32
#endif

/*
 *  Approved way of getting address of caller
 */
#define	svc_getrpccaller(x) (&(x)->xp_rtaddr)
#ifdef _KERNEL
#define	svc_getcaller(x) (&(x)->xp_rtaddr.buf)
#endif

/*
 * Operations defined on an SVCXPRT handle
 *
 * SVCXPRT		 *xprt;
 * struct rpc_msg	 *msg;
 * xdrproc_t		  xargs;
 * caddr_t		  argsp;
#ifdef _KERNEL
 * struct svc_req	 *req;
 * int			  size;
 * caddr_t		  res;
 * struct dupreq	**drpp;
 * struct dupreq	 *drp;
#endif
 */

#ifdef _KERNEL
#define	SVC_RECV(xprt, mp, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (mp), (msg))
#define	svc_recv(xprt, mp, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (mp), (msg))
#else
#define	SVC_RECV(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))
#define	svc_recv(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))
#endif

#define	SVC_STAT(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)
#define	svc_stat(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)

#define	SVC_GETARGS(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))
#define	svc_getargs(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))

#define	SVC_REPLY(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))
#define	svc_reply(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))

#define	SVC_FREEARGS(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))
#define	svc_freeargs(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))

#define	SVC_GETRES(xprt, size)		\
	(*(xprt)->xp_ops->xp_getres)((xprt), (size))
#define	svc_getres(xprt, size)		\
	(*(xprt)->xp_ops->xp_getres)((xprt), (size))

#define	SVC_FREERES(xprt)		\
	(*(xprt)->xp_ops->xp_freeres)(xprt)
#define	svc_freeres(xprt)		\
	(*(xprt)->xp_ops->xp_freeres)(xprt)

#define	SVC_DESTROY(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)
#define	svc_destroy(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)

#ifdef _KERNEL
extern bool_t svc_control(SVCXPRT *, u_int, void *);
/*
 * There are currently no transport-dependent control routines in the
 * kernel.
 */
#else
#define	SVC_CONTROL(xprt, rq, in)				\
	(*(xprt)->xp_ops->xp_control)((xprt), (rq), (in))
#endif

#ifdef _KERNEL
#define	SVC_DUP(xprt, req, res, size, drpp)		\
	(*(xprt)->xp_ops->xp_dup)(req, res, size, drpp)
#define	svc_dup(xprt, req, res, size, drpp)		\
	(*(xprt)->xp_ops->xp_dup)(req, res, size, drpp)

#define	SVC_DUPDONE(xprt, dr, res, size, status)	\
	(*(xprt)->xp_ops->xp_dupdone)(dr, res, size, status)
#define	svc_dupdone(xprt, dr, res, size, status)	\
	(*(xprt)->xp_ops->xp_dupdone)(dr, res, size, status)

#define	SVC_CLONE(xprt, new_xprt, p2buf)	\
	(*(xprt)->xp_ops->xp_clone)(xprt, new_xprt, p2buf)
#define	svc_clone(xprt, dr, res, size, status)  \
	(*(xprt)->xp_ops->xp_clone)(dr, res, size, status)

#define	SVC_CLONE_DESTROY(xprt) \
		(*(xprt)->xp_ops->xp_clone_destroy)(xprt)
#define	svc_clone_destroy(xprt) \
		(*(xprt)->xp_ops->xp_clone_destroy)(xprt)
#endif

#ifndef _KERNEL
#ifdef __STDC__
extern bool_t	rpc_reg(const u_long, const u_long,
		const u_long, char *(*)(char *),
		const xdrproc_t, const xdrproc_t,
		const char *);
#else
extern bool_t	rpc_reg();
#endif
#endif

/*
 * Service registration
 *
 * svc_reg(xprt, prog, vers, dispatch, nconf)
 *	const SVCXPRT *xprt;
 *	const u_long prog;
 *	const u_long vers;
 *	const void (*dispatch)();
 *	const struct netconfig *nconf;
 */

#ifdef __STDC__

extern bool_t	svc_reg(const SVCXPRT *, const u_long, const u_long,
			void (*)(struct svc_req *, SVCXPRT *),
			const struct netconfig *);
#else
extern bool_t	svc_reg();
#endif


/*
 * Service un-registration
 *
 * svc_unreg(prog, vers)
 *	const u_long prog;
 *	const u_long vers;
 */

#ifdef __STDC__
extern void	svc_unreg(const u_long, const u_long);
#else
extern void	svc_unreg();
#endif


/*
 * Transport registration.
 *
 * xprt_register(xprt)
 *	const SVCXPRT *xprt;
 */

#ifdef __STDC__
extern void	xprt_register(const SVCXPRT *);
#else
extern void	xprt_register();
#endif


/*
 * Transport un-register
 *
 * xprt_unregister(xprt)
 *	const SVCXPRT *xprt;
 */

#ifdef __STDC__
extern void	xprt_unregister(const SVCXPRT *);
#else
extern void	xprt_unregister();
#endif


/*
 * When the service routine is called, it must first check to see if it
 * knows about the procedure;  if not, it should call svcerr_noproc
 * and return.  If so, it should deserialize its arguments via
 * SVC_GETARGS (defined above).  If the deserialization does not work,
 * svcerr_decode should be called followed by a return.  Successful
 * decoding of the arguments should be followed the execution of the
 * procedure's code and a call to svc_sendreply.
 *
 * Also, if the service refuses to execute the procedure due to too-
 * weak authentication parameters, svcerr_weakauth should be called.
 * Note: do not confuse access-control failure with weak authentication!
 *
 * NB: In pure implementations of rpc, the caller always waits for a reply
 * msg.  This message is sent when svc_sendreply is called.
 * Therefore pure service implementations should always call
 * svc_sendreply even if the function logically returns void;  use
 * xdr.h - xdr_void for the xdr routine.  HOWEVER, connectionful rpc allows
 * for the abuse of pure rpc via batched calling or pipelining.  In the
 * case of a batched call, svc_sendreply should NOT be called since
 * this would send a return message, which is what batching tries to avoid.
 * It is the service/protocol writer's responsibility to know which calls are
 * batched and which are not.  Warning: responding to batch calls may
 * deadlock the caller and server processes!
 */

#ifdef __STDC__
extern bool_t	svc_sendreply(const SVCXPRT *, const xdrproc_t, const caddr_t);
extern void	svcerr_decode(const SVCXPRT *);
extern void	svcerr_weakauth(const SVCXPRT *);
extern void	svcerr_noproc(const SVCXPRT *);
extern void	svcerr_progvers(const SVCXPRT *, const u_long, const u_long);
extern void	svcerr_auth(const SVCXPRT *, const enum auth_stat);
extern void	svcerr_noprog(const SVCXPRT *);
extern void	svcerr_systemerr(const SVCXPRT *);
#else
extern bool_t	svc_sendreply();
extern void	svcerr_decode();
extern void	svcerr_weakauth();
extern void	svcerr_noproc();
extern void	svcerr_progvers();
extern void	svcerr_auth();
extern void	svcerr_noprog();
extern void	svcerr_systemerr();
#endif

/*
 * Lowest level dispatching -OR- who owns this process anyway.
 * Somebody has to wait for incoming requests and then call the correct
 * service routine.  The routine svc_run does infinite waiting; i.e.,
 * svc_run never returns.
 * Since another (co-existant) package may wish to selectively wait for
 * incoming calls or other events outside of the rpc architecture, the
 * routine svc_getreq is provided.  It must be passed readfds, the
 * "in-place" results of a select call (see select, section XXX).
 */

#ifndef _KERNEL
/*
 * Global keeper of rpc service descriptors in use
 * dynamic; must be inspected before each call to select
 */
extern fd_set svc_fdset;
#define	svc_fds svc_fdset.fds_bits[0]	/* compatibility */
#endif /* _KERNEL */
/*
 * a small program implemented by the svc_rpc implementation itself;
 * also see clnt.h for protocol numbers.
 */

#ifdef __STDC__
#ifndef _KERNEL
extern void	svc_getreq(int);
extern void	svc_getreqset(fd_set *);	/* takes fdset instead of int */
extern void	svc_run(void);
#endif

#else
#ifndef _KERNEL
extern void	rpctest_service();
extern void	svc_getreqset();
#endif
#endif


#ifndef _KERNEL
/*
 * These are the existing service side transport implementations
 */
/*
 * Transport independent svc_create routine.
 */
#ifdef __STDC__
extern  int svc_create(void (*)(struct svc_req *, SVCXPRT *),
const u_long, const u_long, const char *);
/*
 * 	void (*dispatch)();		-- dispatch routine
 *	const u_long prognum;			-- program number
 *	const u_long versnum;			-- version number
 *	const char *nettype;			-- network type
 */
#else
extern int svc_create();
#endif


/*
 * Generic server creation routine. It takes a netconfig structure
 * instead of a nettype.
 */

#ifdef __STDC__
extern  SVCXPRT	*svc_tp_create(void (*)(struct svc_req *, SVCXPRT *),
const u_long, const u_long, const struct netconfig *);
	/*
	 * void (*dispatch)();		-- dispatch routine
	 * const u_long prognum;			-- program number
	 * const u_long versnum;			-- version number
	 * const struct netconfig *nconf;	-- netconfig structure
	 */
#else
extern SVCXPRT	*svc_tp_create();
#endif

/*
 * Generic TLI create routine
 */
#ifdef __STDC__
extern  SVCXPRT *svc_tli_create(const int, const struct netconfig *, const
struct t_bind *, const u_int, const u_int);
/*
 *	const int fd;				-- connection end point
 *	const struct netconfig *nconf;	-- netconfig structure for network
 *	const struct t_bind *bindaddr;	-- local bind address
 *	const u_int sendsz;			-- max sendsize
 *	const u_int recvsz;			-- max recvsize
 */
#else
extern SVCXPRT *svc_tli_create();
#endif


/*
 * Connectionless and connectionful create routines
 */
#ifdef __STDC__
extern  SVCXPRT	*svc_vc_create(const int, const u_int, const u_int);
/*
 *	const int fd;				-- open connection end point
 *	const u_int sendsize;			-- max send size
 *	const u_int recvsize;			-- max recv size
 */

extern  SVCXPRT	*svc_dg_create(const int, const u_int, const u_int);
	/*
	 * const int fd;				-- open connection
	 * const u_int sendsize;			-- max send size
	 * const u_int recvsize;			-- max recv size
	 */
#else
extern SVCXPRT	*svc_vc_create();
extern SVCXPRT	*svc_dg_create();
#endif

/*
 * the routine takes any *open* TLI file
 * descriptor as its first input and is used for open connections.
 */
#ifdef __STDC__
extern  SVCXPRT *svc_fd_create(const int, const u_int, const u_int);
/*
 * 	const int fd;				-- open connection end point
 * 	const u_int sendsize;			-- max send size
 * 	const u_int recvsize;			-- max recv size
 */
#else
extern SVCXPRT *svc_fd_create();
#endif

/*
 * Memory based rpc (for speed check and testing)
 */
#ifdef __STDC__
extern SVCXPRT *svc_raw_create(void);
#else
extern SVCXPRT *svc_raw_create();
#endif

/*
 * svc_dg_enable_cache() enables the cache on dg transports.
 */
#ifdef __STDC__
int svc_dg_enablecache(SVCXPRT *, const u_long);
#else
int svc_dg_enablecache();
#endif

#ifdef PORTMAP
/* For backward compatibility */
#include <rpc/svc_soc.h>
#endif

#else	/* _KERNEL */

/*
 * kernel based rpc
 */

#include <rpc/svc_soc.h>

extern int	svc_tli_kcreate(struct file *, u_int, SVCXPRT **);
extern int	svc_clts_kcreate(struct file *, u_int, struct T_info_ack *,
			SVCXPRT **);
extern int	svc_cots_kcreate(struct file *, u_int, struct T_info_ack *,
			SVCXPRT **);
extern void	svc_queuereq(queue_t *q, mblk_t *mp);
extern void	svc_queueclose(queue_t *q);
extern int	svc_reserve_thread(SVCXPRT *clone_xprt);
extern void	svc_unreserve_thread(SVCXPRT *clone_xprt);
extern callb_cpr_t *svc_detach_thread(SVCXPRT *clone_xprt);

#endif /* !_KERNEL */

/*
 * For user level MT hot server functions
 */
#ifndef _KERNEL

/*
 * Different MT modes
 */
#define	RPC_SVC_MT_NONE		0	/* default, single-threaded */
#define	RPC_SVC_MT_AUTO		1	/* automatic MT mode */
#define	RPC_SVC_MT_USER		2	/* user MT mode */

#ifdef __STDC__
void svc_done(SVCXPRT *);
#else
void svc_done();
#endif

#endif /* !_KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* !_RPC_SVC_H */
